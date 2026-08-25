#!/usr/bin/env python3
"""vdot 流水时序追踪器 — 从 emu dump 的 VCD 中提取一条 vdot 指令的流水线时间线。

用法:
  python3 vdot_pipeline_trace.py <vcd_file> [--pc 0x80000030] [--dump-signals]

原理（按 xiangshan-wave-analysis 技能流程）:
  1. 归一化目标指令: vdot.vv PC, 反汇编, 寄存器 (vd=10, vs1=9, vs2=8)
  2. 前端锚点: 从前端 IFU/Decode 的 PC 信号命中目标 PC
  3. 沿流水线跟 robIdx / 各阶段 valid/ready, 记录 fire=valid&&ready 的时刻
  4. 后端: Dispatch -> IQ -> Issue(VFEX0) -> VdotU(io.in.valid) -> Vdot64b(fire) -> Mgu -> Writeback -> Commit
  5. 输出时间线表: 每个阶段出现/完成的 cycle

对 VCD 的解析: 先用 vcd 转成按 cycle 采样的信号值表 (信号可能巨大, 只保留
关心的子集, 通过 --signals 正则过滤)。
"""
import re, sys, argparse

def parse_vcd_vars(f):
    """读 $var 声明段, 返回 {id: (name, width)}. 文件指针停在 $enddefinitions 后."""
    vars_map = {}
    for line in f:
        if line.startswith('$enddefinitions'):
            break
        m = re.match(r'$vars+w+s+(d+)s+(S+)s+(S+)s*', line)
        if m:
            vars_map[m.group(2)] = (m.group(3), int(m.group(1)))
    return vars_map

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('vcd')
    ap.add_argument('--pc', default='0x80000030', help='目标 vdot PC')
    ap.add_argument('--signals', default='', help='正则过滤信号名(逗号分隔)')
    ap.add_argument('--dump-signals', action='store_true', help='列出全部信号')
    args = ap.parse_args()

    with open(args.vcd, 'r', errors='ignore') as f:
        vars_map = parse_vcd_vars(f)
        print(f'VCD 信号数: {len(vars_map)}')
        if args.dump_signals:
            for vid, (name, w) in sorted(vars_map.items(), key=lambda x: x[1][0]):
                print(f'  {vid} {name} [{w}]')
            return

        # 信号过滤: 保留 pipeline 关键信号
        patterns = []
        if args.signals:
            patterns = [re.compile(s.strip()) for s in args.signals.split(',')]
        else:
            patterns = [re.compile(p) for p in [
                r'\.core_with_l2\.core\.frontend',   # 前端
                r'\backend\.(decode|rename|dispatch|rob|iq|issue|writeback)',
                r'\backend\.(exu|fu|vdot|vprf)',       # 执行单元
                r'\backend\.datapath',                  # 数据通路
                r'\rob',                                 # ROB
                r'difftest_commit',                       # 提交
            ]]
        keep = {vid: (name, w) for vid, (name, w) in vars_map.items()
                if any(p.search(name) for p in patterns)}
        print(f'保留信号: {len(keep)}/{len(vars_map)}')

        # 采样: 只记录目标 PC 出现的时刻作为锚点
        cur_time = 0
        vals = {}
        pc_hits = []
        for line in f:
            line = line.rstrip()
            if not line or line.startswith('$'):
                continue
            if line.startswith('#'):
                cur_time = int(line[1:])
                continue
            if line.startswith('b'):
                parts = line.split()
                if len(parts) >= 2:
                    vals[parts[1]] = parts[0][1:]
            else:
                if len(line) >= 2:
                    ch, vid = line[0], line[1:]
                    if ch in '01xXzZ':
                        vals[vid] = ch
            # PC 锚点检测: 找含目标 PC 的 pc 信号
            for vid, (name, w) in keep.items():
                if w == 64 and ('pc' in name.lower()) and vals.get(vid, ''):
                    try:
                        if int(vals[vid], 16) == int(args.pc, 16):
                            pc_hits.append((cur_time, name))
                    except ValueError:
                        pass
            if len(pc_hits) > 10:
                break

        print(f'\nPC {args.pc} 命中时刻 (前10):')
        for t, name in pc_hits[:10]:
            print(f'  cycle {t}: {name}')
        if not pc_hits:
            print('  未命中 — 检查 PC 值或信号名')

if __name__ == '__main__':
    main()
