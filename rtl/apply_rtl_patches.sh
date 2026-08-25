#!/usr/bin/env bash
# ============================================================================
# apply_rtl_patches.sh - apply Chisel RTL patches in rtl/ to XiangShan/yunsuan source tree
#
# rtl/ mirrors upstream paths:
#   rtl/xs-kunminghu-v2/src/...  -> <xiangshan>/src/...
#   rtl/yunsuan/src/...          -> <yunsuan>/src/...
#
# Usage:
#   ./apply_rtl_patches.sh --xiangshan <XiangShan root> --yunsuan <yunsuan root>
#   ./apply_rtl_patches.sh --xiangshan <root> --dry-run
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"   # 本脚本在 rtl/ 下
RTL_DIR="$(dirname "$SCRIPT_DIR")/rtl"                # 仓库根下的 rtl/

XIANGSHAN=""
YUNSUAN=""
DRY_RUN=0
BACKUP=0

usage() {
  cat <<EOF
Usage: $0 --xiangshan <path> [--yunsuan <path>] [--dry-run] [--backup]

  --xiangshan <path>   XiangShan main repo root (contains src/, build.sc)
  --yunsuan <path>     yunsuan submodule root (optional)
  --dry-run            preview only, no copy
  --backup             create .orig backups in target
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --xiangshan) XIANGSHAN="$2"; shift 2 ;;
    --yunsuan)   YUNSUAN="$2";   shift 2 ;;
    --dry-run)   DRY_RUN=1;      shift ;;
    --backup)    BACKUP=1;       shift ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "unknown option: $1"; usage; exit 1 ;;
  esac
done

if [ -z "$XIANGSHAN" ]; then
  echo "[error] --xiangshan is required"; usage; exit 1
fi

[ -d "$RTL_DIR" ] || { echo "[error] rtl/ not found: $RTL_DIR"; exit 1; }
[ -d "$XIANGSHAN" ] || { echo "[error] xiangshan path not found: $XIANGSHAN"; exit 1; }
if [ -n "$YUNSUAN" ]; then
  [ -d "$YUNSUAN" ] || { echo "[error] yunsuan path not found: $YUNSUAN"; exit 1; }
fi

if [ ! -d "$XIANGSHAN/src/main/scala/xiangshan" ]; then
  echo "[warn] $XIANGSHAN/src/main/scala/xiangshan missing; may not be XiangShan main repo"
fi

apply_tree() {
  local src_root="$1" dst_root="$2" label="$3"
  if [ ! -d "$src_root" ]; then
    echo "[skip] $label: no patch dir $src_root"; return
  fi
  echo "=============================================================="
  echo "[$label] source: $src_root"
  echo "[$label] target: $dst_root"
  mkdir -p "$dst_root"
  echo "--------------------------------------------------------------"
  copied=0; skipped=0; added=0
  while IFS= read -r f; do
    rel="${f#$src_root/}"
    dst="$dst_root/$rel"
    mkdir -p "$(dirname "$dst")"
    if [ -f "$dst" ]; then
      if diff -q "$f" "$dst" >/dev/null 2>&1; then
        echo "  [same] $rel (already identical)"; skipped=$((skipped+1)); continue
      else
        echo "  [mod]  $rel"
      fi
    else
      echo "  [new]  $rel"
    fi
    if [ $DRY_RUN -eq 1 ]; then
      added=$((added+1)); continue
    fi
    if [ $BACKUP -eq 1 ] && [ -f "$dst" ]; then
      cp -p "$dst" "$dst.orig"
    fi
    cp -p "$f" "$dst"
    copied=$((copied+1))
  done < <(find "$src_root" -type f | sort)
  if [ $DRY_RUN -eq 1 ]; then
    echo "[dry-run] would apply $added file(s); $skipped unchanged; nothing modified."
  else
    echo "[done] $label: copied $copied, unchanged $skipped."
  fi
}

echo "xiangshan=$XIANGSHAN  yunsuan=${YUNSUAN:-<skip>}  dry_run=$DRY_RUN backup=$BACKUP"
apply_tree "$RTL_DIR/xs-kunminghu-v2" "$XIANGSHAN" "XiangShan main"
if [ -n "$YUNSUAN" ]; then
  apply_tree "$RTL_DIR/yunsuan" "$YUNSUAN" "yunsuan"
fi
echo "done. verify with: git -C $XIANGSHAN status --short"
