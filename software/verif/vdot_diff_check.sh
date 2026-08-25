#!/usr/bin/env bash
# ============================================================================
# vdot_diff_check.sh — P1 vdot 差分复测脚本 (emu + NEMU difftest)
#
# 用法:
#   ./vdot_diff_check.sh [--emu <path>] [--diff <nemu.so>] [--img <bin>]...
#   或设置环境变量: NOOP_HOME, EMU, NEMU_SO, VTEST_IMGS(空格分隔)
#
# 行为:
#   1. 校验 emu / NEMU .so / 镜像存在
#   2. 逐个镜像运行 emu -i <img> --diff <so>, 解析 HIT GOOD TRAP 与 trap code
#   3. 差分失配(MISMATCH/abort)记为 FAIL
#   4. 汇总输出 PASS/FAIL 与下一步提示
#
# 预期: vdot_test.bin (单次) trap code 0x0a; (两次累加) trap code 0x14
#   详见 P1_DIFF_CHECKLIST.md
# ============================================================================
set -u

NOOP_HOME="${NOOP_HOME:-$(cd "$(dirname "$0")/../../xs-kunminghu-v2" && pwd)}"
EMU="${EMU:-$NOOP_HOME/build/emu}"
NEMU_SO="${NEMU_SO:-}"
IMGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --emu)  EMU="$2"; shift 2 ;;
    --diff) NEMU_SO="$2"; shift 2 ;;
    --img)  IMGS+=("$2"); shift 2 ;;
    -h|--help)
      sed -n '1,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "[vdot_diff_check] 未知参数: $1 (见 --help)"; exit 2 ;;
  esac
done
[ -z "$NEMU_SO" ] && NEMU_SO="$NOOP_HOME/ready-to-run/riscv64-nemu-interpreter-so"
if [ -z "${IMGS[*]:-}" ] && [ -n "${VTEST_IMGS:-}" ]; then
  IMGS=($VTEST_IMGS)
fi
[ "${#IMGS[@]}" -eq 0 ] && IMGS=("$NOOP_HOME/ready-to-run/vdot_test.bin")

echo "=== vdot 差分复测 ==="
echo "emu     : $EMU"
echo "nemu.so : $NEMU_SO"
for f in "$EMU" "$NEMU_SO"; do
  [ -f "$f" ] || { echo "[FAIL] 缺少文件: $f"; exit 2; }
done

pass=0; fail=0
for img in "${IMGS[@]}"; do
  [ -f "$img" ] || { echo "[FAIL] 缺少镜像: $img"; fail=$((fail+1)); continue; }
  echo "--- $img ---"
  out="$("$EMU" -i "$img" --diff "$NEMU_SO" 2>&1)"
  rc=$?
  trap_code="$(echo "$out" | grep -oE 'HIT GOOD TRAP' | head -1)"
  mismatch="$(echo "$out" | grep -icE 'mismatch|abort|fail' || true)"
  code="$(echo "$out" | grep -oE 'code *= *[0-9]+' | head -1 | grep -oE '[0-9]+')"
  if [ "$rc" -eq 0 ] && [ -n "$trap_code" ] && [ "$mismatch" -eq 0 ]; then
    echo "[PASS] $img  GOOD TRAP (exit=$rc)${code:+ trap_code=$code}"
    pass=$((pass+1))
  else
    echo "[FAIL] $img  exit=$rc trap='$trap_code' mismatch=$mismatch"
    echo "$out" | tail -12
    fail=$((fail+1))
  fi
done

echo "=== 汇总: PASS=$pass FAIL=$fail ==="
if [ "$fail" -eq 0 ]; then
  echo "[OK] 核心差分通过。下一步见 P1_DIFF_CHECKLIST.md §2-§5 (边界/随机/回归)。"
  exit 0
else
  echo "[!!] 存在失败项，请对照 P1_DIFF_CHECKLIST.md 定位；更新 08_CheckList §3.3。"
  exit 1
fi
