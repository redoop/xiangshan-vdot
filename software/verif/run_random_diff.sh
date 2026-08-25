#!/usr/bin/env bash
# ============================================================================
# run_random_diff.sh — vdot 随机差分测试 runner
#
# 流程: 生成(gen_vdot_tests.py) -> 汇编(.S -> .bin) -> 逐例运行 -> 汇总
# 运行模式:
#   1) 差分模式(默认, Linux):  emu -i <bin> --diff <nemu.so>   (RTL vs NEMU)
#   2) NEMU-only 模式(本机可跑): riscv64-nemu-interpreter -b <bin>   (验证 golden 与期望 trap code)
#
# 用法:
#   ./run_random_diff.sh --seed N --cases M --outdir DIR \
#                        [--emu <path>] [--diff <nemu.so>] [--gcc <prefix>] \
#                        [--nemu <interpreter>] [--nemu-only] [--directed] [--parallel P]
# 依赖: python3, riscv64-*-gcc (汇编), NEMU/emu
# ============================================================================
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
SEED=20260817; CASES=50; OUT="$(mktemp -d /tmp/vdot_rand.XXXXXX)"
EMU=""; DIFF=""; GCC=""; NEMU=""; NEMU_ONLY=0; DIRECTED=0; PARALLEL=4

while [ $# -gt 0 ]; do
  case "$1" in
    --seed) SEED="$2"; shift 2 ;; --cases) CASES="$2"; shift 2 ;;
    --outdir) OUT="$2"; shift 2 ;;
    --emu) EMU="$2"; shift 2 ;; --diff) DIFF="$2"; shift 2 ;;
    --gcc) GCC="$2"; shift 2 ;; --nemu) NEMU="$2"; shift 2 ;;
    --nemu-only) NEMU_ONLY=1; shift ;; --directed) DIRECTED=1; shift ;;
    --parallel) PARALLEL="$2"; shift 2 ;;
    -h|--help) sed -n '1,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "[run_random_diff] 未知参数: $1"; exit 2 ;;
  esac
done
[ -z "$GCC" ] && { command -v riscv64-unknown-elf-gcc >/dev/null && GCC=riscv64-unknown-elf-gcc || GCC=riscv64-elf-gcc; }
command -v "$GCC" >/dev/null || { echo "[FAIL] 找不到 riscv gcc: $GCC"; exit 2; }

echo "=== vdot 随机差分 (seed=$SEED cases=$CASES out=$OUT${DIRECTED:+, directed}) ==="
DIRECTED_FLAG=""; [ "$DIRECTED" -eq 1 ] && DIRECTED_FLAG="--directed"
python3 "$DIR/gen_vdot_tests.py" --seed "$SEED" --cases "$CASES" --outdir "$OUT" $DIRECTED_FLAG || exit 2

# 汇编 .S -> .bin
echo "--- assemble ---"
assemble_one() {
  local s="$1"
  "$GCC" -march=rv64gcv -mabi=lp64d -nostdlib -T "$OUT/vdot_rand.ld" \
         -o "${s%.S}" "$s" 2>/dev/null || return 1
  riscv64-elf-objcopy -O binary "${s%.S}" "${s%.S}.bin" || return 1
  return 0
}
export OUT GCC
ok=0; fail=0
for s in "$OUT"/vdot_*.S; do assemble_one "$s" && ok=$((ok+1)) || { echo "[ASM FAIL] $s"; fail=$((fail+1)); }; done
echo "assemble: ok=$ok fail=$fail"

# 逐例运行并核对退出码 (自校验: PASS=0x5A5A=23130, FAIL=0xDEAD=57005; manifest 第2列)
echo "--- run ---"
declare -A EXP
while IFS=$'\t' read -r c pass rest; do [ "$c" != "case" ] && EXP[$c]="$pass"; done < "$OUT/manifest.tsv"
pass=0; failn=0
run_one() {
  local bin="$1" c; c="$(basename "$bin" .bin | sed 's/.*_//; s/^0*//')"
  local want="${EXP[$c]:-23130}" code= rc=
  if [ "$NEMU_ONLY" -eq 1 ]; then
    # NEMU 独立模式: nemu_trap 的 trap code 打印在 stdout ("trap code:23130"), 进程退出码恒为 1
    out="$("$NEMU" -b "$bin" 2>&1)"; rc=$?
    code="$(echo "$out" | grep -oE 'trap code[: ]*-*[0-9]+' | grep -oE -- '-?[0-9]+' | head -1)"
  else
    out="$("$EMU" -i "$bin" --diff "$DIFF" 2>&1)"; rc=$?
    code="$(echo "$out" | grep -oE 'trap code[: ]*-*[0-9]+' | grep -oE -- '-?[0-9]+' | head -1)"
    if echo "$out" | grep -qiE 'mismatch|abort'; then code="MISMATCH"; fi
  fi
  if [ "$code" = "$want" ]; then
    echo "[PASS] $bin  code=$code (pass_code=$want)"; return 0
  elif [ "$code" = "57005" ]; then
    echo "[FAIL] $bin  lane0 自校验不匹配 (code=0xDEAD)"; echo "$out" | tail -6 > "${bin}.log"; return 1
  else
    echo "[FAIL] $bin  code=${code:-无} (want $want) rc=$rc"; echo "$out" | tail -6 > "${bin}.log"; return 1
  fi
}
export -f run_one EMU DIFF NEMU NEMU_ONLY EXP
if [ "$NEMU_ONLY" -eq 1 ]; then
  [ -n "$NEMU" ] && [ -x "$NEMU" ] || { echo "[FAIL] NEMU 不可用: $NEMU"; exit 2; }
  for b in "$OUT"/*.bin; do run_one "$b" && pass=$((pass+1)) || failn=$((failn+1)); done
else
  [ -n "$EMU" ] && [ -x "$EMU" ] || { echo "[FAIL] emu 不可用: $EMU"; exit 2; }
  [ -n "$DIFF" ] && [ -f "$DIFF" ] || { echo "[FAIL] nemu.so 不可用: $DIFF"; exit 2; }
  export OUT
  ls "$OUT"/*.bin | xargs -P "$PARALLEL" -I{} bash -c 'run_one "$1" && echo P || echo F' _ {} > "$OUT/run.out" 2>&1
  pass=$(grep -c '^P$' "$OUT/run.out"); failn=$(grep -c '^F$' "$OUT/run.out")
  cat "$OUT/run.out" | sed 's/^P$/  [PASS]/;s/^F$/  [FAIL]/' | head -40
fi

echo "=== 汇总: PASS=$pass FAIL=$failn (seed=$SEED, out=$OUT) ==="
[ "$failn" -eq 0 ] && echo "[OK] 随机差分全部通过" && exit 0 || exit 1
