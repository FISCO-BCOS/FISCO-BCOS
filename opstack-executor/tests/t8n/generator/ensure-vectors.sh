#!/usr/bin/env bash
# =============================================================================
# 确保 opstack t8n 参考向量落地：op-geth@pin 就位（复用或克隆）→ 跑 regen.sh。
#
# 本地与 CI 共用同一逻辑：GitHub composite action (.github/actions/opstack-t8n-regen)
# 只负责 GitHub 专属部分（actions/setup-go + actions/cache），然后委托本脚本，
# 因此「本地跑本脚本 == CI 里 composite action 的行为」。
#
# 用法
#   bash opstack-executor/tests/t8n/generator/ensure-vectors.sh
#   OPGETH=/path/to/op-geth bash .../ensure-vectors.sh   # 指定现有 op-geth checkout
#   ./.../ensure-vectors.sh                              # 脚本可执行后直接跑
#
# 环境变量
#   OPGETH           op-geth checkout 路径。缺省用 ${OPGETH_CACHE_DIR}/op-geth-<pin>
#                    （默认 ~/.cache/op-geth/op-geth-<pin>，自动克隆并钉住 pin）。
#   OPGETH_CACHE_DIR 覆盖本地缓存目录（仅当 OPGETH 未设置时生效）。
#   OPGETH_PIN       覆盖 pin；缺省从 regen.sh 提取（单一事实源）。
#
# 退出码
#   0  = 向量已生成且 regen.sh 全部校验通过；非 0 = 失败（见 regen.sh 判据）。
# =============================================================================
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
REGEN="$REPO_ROOT/opstack-executor/tests/t8n/generator/regen.sh"
[ -f "$REGEN" ] || { echo "找不到 regen.sh: $REGEN" >&2; exit 1; }

PIN="${OPGETH_PIN:-$(sed -nE 's/^PIN="([0-9a-f]{40})"/\1/p' "$REGEN")}"
[ -n "$PIN" ] || { echo "无法从 regen.sh 提取 PIN" >&2; exit 1; }

USER_OPGETH="${OPGETH:-}"   # 用户显式传入则视为自有 checkout，绝不删除
if [ -z "$USER_OPGETH" ]; then
  CACHE_ROOT="${OPGETH_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/op-geth}"
  OPGETH="$CACHE_ROOT/op-geth-$PIN"
fi

command -v go >/dev/null 2>&1 || { echo "需要 Go 工具链（regen.sh 要在 op-geth 模块内 go build）" >&2; exit 1; }

# 确保 $OPGETH 是一个 HEAD==pin 且干净（regen.sh 两条前置）的 checkout。
#   - 已就位且 pin 正确：清理 regen 遗留产物（cmd/opt8n-ref 目录 + 顶层二进制），
#     保证 git status 干净后复用。
#   - 未就位：浅克隆（--filter=blob:none）→ fetch 精确 SHA → checkout。
#   - 已就位但 pin 不符：
#       用户显式传入（$USER_OPGETH）→ 视为自有 checkout，直接报错，绝不删除；
#       脚本自管缓存目录 → 视为陈旧缓存，rm -rf 后重建。
ensure_opgeth() {
  local existing=""
  [ -d "$OPGETH/.git" ] && existing="$(git -C "$OPGETH" rev-parse HEAD 2>/dev/null || true)"
  if [ -n "$existing" ] && [ "$existing" = "$PIN" ]; then
    echo "复用 op-geth@${PIN:0:8}: $OPGETH"
    git -C "$OPGETH" clean -fdx -- cmd/opt8n-ref >/dev/null 2>&1 || true
    rm -f "$OPGETH/opt8n-ref"
  elif [ -n "$existing" ] && [ -n "$USER_OPGETH" ]; then
    echo "OPGETH 指向的 checkout 在 ${existing:0:8}，需要 ${PIN:0:8}。请改用正确 pin 的 checkout，或不要设置 OPGETH 让脚本管理缓存目录。" >&2
    exit 1
  else
    mkdir -p "$(dirname "$OPGETH")"
    rm -rf "$OPGETH"
    echo "克隆 op-geth@${PIN:0:8} -> $OPGETH"
    git clone --filter=blob:none https://github.com/ethereum-optimism/op-geth "$OPGETH"
    git -C "$OPGETH" fetch --depth 1 origin "$PIN"
    git -C "$OPGETH" checkout FETCH_HEAD
  fi
}

ensure_opgeth
echo ">>> 运行 regen.sh（OPGETH=${OPGETH}）"
OPGETH="$OPGETH" bash "$REGEN"
