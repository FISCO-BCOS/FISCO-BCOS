#!/usr/bin/env bash
# 整批重生成仪式（spec rev.4 + Phase-2 线 B Task 5 预编译矩阵 + Task 6 增强语料）。
# 退出 0 ⇔ 全向量生成成功 ∧ manifest 与 cases∪三模式产物集合相等 ∧ 工作树与入库字节等同。
# Task 7 集成：三模式（corrupt/static/invalid-tx/chain）接入 + diff 源重定义 + golden 覆盖。
set -euo pipefail
OPGETH="${OPGETH:-/Users/octopus/octo/code/blockchain-impl/op-geth}"
PIN="e8800cffe53d459cde8a07c8e8f1de9d86e79e07"
GEN_DIR="$(cd "$(dirname "$0")" && pwd)"
T8N_DIR="$(dirname "$GEN_DIR")"
REPO_ROOT="$(git -C "$GEN_DIR" rev-parse --show-toplevel)"
SCRATCH="$OPGETH/cmd/opt8n-ref"
N_CHAIN=3

[ "$(git -C "$OPGETH" rev-parse HEAD)" = "$PIN" ] || { echo "op-geth HEAD != $PIN" >&2; exit 1; }
# ⚠️ 检查须在 opt8n-ref 二进制/目录被本脚本创建前进行：上一轮正常退出已被 cleanup trap
# rm 掉，此处只拦「有人手工把东西丢进 op-geth 工作树」。
[ -z "$(git -C "$OPGETH" status --porcelain)" ] || { echo "op-geth worktree dirty" >&2; exit 1; }

cleanup() { rc=$?; rm -rf "$SCRATCH"; rm -f "$OPGETH/opt8n-ref"; exit $rc; }
trap cleanup EXIT

rm -rf "$SCRATCH"; cp -r "$GEN_DIR" "$SCRATCH"
( cd "$OPGETH" && go build ./cmd/opt8n-ref )

"$OPGETH/opt8n-ref" --write-cases "$T8N_DIR/cases"          # 单源重发全部 case 文件（含 legacy_transfer）

# ── 逐 case 向量 + engine golden ─────────────────────────────────────────
# 动态计数（Task 7 Step 1：原 `[ "$n" -eq 77 ]` 硬计数改动态）：每个 .in.json 必须产出
# vectors/<base>.json 与 golden/engine/<base>.golden.json，缺任一 = FAILURE。
for in_json in "$T8N_DIR"/cases/*.in.json; do               # fork 在文件名/_info 内，无 --fork
  base="$(basename "$in_json" .in.json)"
  "$OPGETH/opt8n-ref" --input "$in_json" --output "$T8N_DIR/vectors/${base}.json" \
    --golden-output "$T8N_DIR/golden/engine/${base}.golden.json" \
    --op-geth-commit "$PIN"
done
missing=0
for in_json in "$T8N_DIR"/cases/*.in.json; do
  base="$(basename "$in_json" .in.json)"
  [ -f "$T8N_DIR/vectors/${base}.json" ] || { echo "missing vector ${base}.json" >&2; missing=$((missing+1)); }
  [ -f "$T8N_DIR/golden/engine/${base}.golden.json" ] || { echo "missing golden ${base}.golden.json" >&2; missing=$((missing+1)); }
done
[ "$missing" -eq 0 ] || exit 1

# ── 三模式派生向量（Task 7 Step 1）────────────────────────────────────────
# corrupt：§4a 6 字段 × isthmus/jovian 两 base → invalid_<base>_<field>.json（12）
"$OPGETH/opt8n-ref" --mode=corrupt --base isthmus_transfer_basic --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
"$OPGETH/opt8n-ref" --mode=corrupt --base jovian_transfer_basic --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
# static：§4c 12 项（item 3/12 生成但强制不入 manifest——GoldenSample loader 不可表达）
"$OPGETH/opt8n-ref" --mode=static --base isthmus_transfer_basic --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
# invalid-tx：9 kinds × 2 forks → invalid_<fork>_<kind>.json（18）
for kind in intrinsic_gas nonce_low nonce_high insufficient_funds fee_cap_low sender_no_eoa setcode_create empty_auth_list blob; do
  "$OPGETH/opt8n-ref" --mode=invalid-tx --base "isthmus_${kind}" --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
  "$OPGETH/opt8n-ref" --mode=invalid-tx --base "jovian_${kind}" --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
done
# chain：线性 + fork + break（N=3）。fork/break 带 invalid_ 前缀（E2E invalid 子集消费）。
"$OPGETH/opt8n-ref" --mode="chain:${N_CHAIN}" --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
"$OPGETH/opt8n-ref" --mode="chain:${N_CHAIN}:fork" --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"
"$OPGETH/opt8n-ref" --mode="chain:${N_CHAIN}:break" --out-dir "$T8N_DIR/vectors" --op-geth-commit "$PIN"

"$OPGETH/opt8n-ref" --chain-output-dir "$T8N_DIR/golden/engine/chained" \
  --op-geth-commit "$PIN"                                    # 链式对 golden（chainA/B + jovianChainA/B）

# ── manifest 幂等自动追加（Task 6 Step 1）─────────────────────────────────
# append-if-absent：已注册的向量（非注释非空行）不再追加；static item 3/12 强制排除。
append_if_absent() {
  local manifest="$1"; local comment="$2"; shift 2
  local pending=()
  for f in "$@"; do
    if ! grep -qxF "$f" "$manifest"; then
      pending+=("$f")
    fi
  done
  if [ "${#pending[@]}" -gt 0 ]; then
    {
      printf '\n# %s\n' "$comment"
      printf '%s\n' "${pending[@]}"
    } >> "$manifest"
  fi
}

is_unregistered_static() {  # §4c item 3/12：生成但不入 manifest
  case "$1" in
    *_static_3.json|*_static_12.json) return 0 ;;
    *) return 1 ;;
  esac
}

manifest="$T8N_DIR/vectors/manifest.txt"
registerable=()
for f in "$T8N_DIR"/vectors/*.json; do
  base="$(basename "$f")"
  if is_unregistered_static "$base"; then continue; fi
  registerable+=("$base")
done
# 确定性顺序：排序后追加（与 diff 集合比较同序）。
readarray -t sorted < <(printf '%s\n' "${registerable[@]}" | sort)
append_if_absent "$manifest" "Phase-3 enhanced corpus (Task 6): corrupt 12 + static 10 + invalid-tx 18 + chain 6 + legacy 2 = 48 vectors (idempotent; §4c item 3/12 excluded — loader 不可表达)" "${sorted[@]}"

# ── diff 源重定义（Task 7 Step 1，审查 R10）：cases ∪ 三模式产物 == manifest ──
# cases basename 展开（.in.json → .json）∪ 派生名（corrupt/static 注册项/invalid-tx/chain）
# 与 manifest 非注释行比集合相等（防孤儿向量/漏格）。
{
  ls "$T8N_DIR"/cases/*.in.json | xargs -n1 basename | sed 's/\.in\.json$/.json/'
  printf 'invalid_isthmus_transfer_basic_%s.json\n' stateRoot gasUsed receiptsRoot parentHash extraData blockHash
  printf 'invalid_jovian_transfer_basic_%s.json\n' stateRoot gasUsed receiptsRoot parentHash extraData blockHash
  printf 'invalid_isthmus_transfer_basic_static_%d.json\n' 1 2 4 5 6 7 8 9 10
  printf 'invalid_jovian_transfer_basic_static_%d.json\n' 11
  for kind in intrinsic_gas nonce_low nonce_high insufficient_funds fee_cap_low sender_no_eoa setcode_create empty_auth_list blob; do
    printf 'invalid_isthmus_%s.json\n' "$kind"
    printf 'invalid_jovian_%s.json\n' "$kind"
  done
  printf 'isthmus_chain_%d.json\n' "$N_CHAIN"
  printf 'jovian_chain_%d.json\n' "$N_CHAIN"
  printf 'invalid_isthmus_chain_%d_fork.json\n' "$N_CHAIN"
  printf 'invalid_jovian_chain_%d_fork.json\n' "$N_CHAIN"
  printf 'invalid_isthmus_chain_%d_break.json\n' "$N_CHAIN"
  printf 'invalid_jovian_chain_%d_break.json\n' "$N_CHAIN"
} | sort > /tmp/opt8n-left.$$
grep -v '^#' "$manifest" | sed '/^$/d' | sort > /tmp/opt8n-right.$$
if ! diff /tmp/opt8n-left.$$ /tmp/opt8n-right.$$; then
  echo "cases∪modes / manifest set mismatch" >&2
  rm -f /tmp/opt8n-left.$$ /tmp/opt8n-right.$$
  exit 1
fi
rm -f /tmp/opt8n-left.$$ /tmp/opt8n-right.$$

# ── 终步：字节等同（Task 7 Step 1，审查 E9：golden 覆盖扩到 golden/engine/ + chained/）──
git -C "$REPO_ROOT" diff --exit-code -- \
  "$T8N_DIR/cases/" "$T8N_DIR/vectors/" "$T8N_DIR/golden/engine/"   # 终步：字节等同即脚本状态
