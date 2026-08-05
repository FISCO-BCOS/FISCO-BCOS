#!/usr/bin/env bash
# 整批重生成仪式（spec rev.3）。退出 0 ⇔ 33 案全部生成成功 ∧ 工作树与入库 cases+vectors 字节等同。
set -euo pipefail
OPGETH="${OPGETH:-/Users/octopus/octo/code/blockchain-impl/op-geth}"
PIN="e8800cffe53d459cde8a07c8e8f1de9d86e79e07"
GEN_DIR="$(cd "$(dirname "$0")" && pwd)"
T8N_DIR="$(dirname "$GEN_DIR")"
REPO_ROOT="$(git -C "$GEN_DIR" rev-parse --show-toplevel)"
SCRATCH="$OPGETH/cmd/opt8n-ref"

[ "$(git -C "$OPGETH" rev-parse HEAD)" = "$PIN" ] || { echo "op-geth HEAD != $PIN" >&2; exit 1; }
[ -z "$(git -C "$OPGETH" status --porcelain)" ] || { echo "op-geth worktree dirty" >&2; exit 1; }

cleanup() { rc=$?; rm -rf "$SCRATCH"; rm -f "$OPGETH/opt8n-ref"; exit $rc; }
trap cleanup EXIT

rm -rf "$SCRATCH"; cp -r "$GEN_DIR" "$SCRATCH"
( cd "$OPGETH" && go build ./cmd/opt8n-ref )

"$OPGETH/opt8n-ref" --write-cases "$T8N_DIR/cases"          # 单源重发全部 case 文件

n=0
for in_json in "$T8N_DIR"/cases/*.in.json; do               # fork 在文件名/_info 内，无 --fork
  base="$(basename "$in_json" .in.json)"
  "$OPGETH/opt8n-ref" --input "$in_json" --output "$T8N_DIR/vectors/${base}.json" \
    --op-geth-commit "$PIN"
  n=$((n+1))
done
[ "$n" -eq 33 ] || { echo "generated $n cases, want 33" >&2; exit 1; }

# cases ↔ manifest basename 集合相等（防孤儿向量/漏格）
diff <(ls "$T8N_DIR"/cases/*.in.json | xargs -n1 basename | sed 's/\.in\.json$/.json/' | sort) \
     <(grep -v '^#' "$T8N_DIR/vectors/manifest.txt" | sed '/^$/d' | sort) \
  || { echo "cases/manifest set mismatch" >&2; exit 1; }

git -C "$REPO_ROOT" diff --exit-code -- \
  "$T8N_DIR/cases/" "$T8N_DIR/vectors/"                     # 终步：字节等同即脚本状态
