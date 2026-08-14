#!/usr/bin/env bash
set -euo pipefail
# op-block-check：跑代码裁决 wrapper（零新 C++，复用现有 Boost 测试）
REPO="$(git rev-parse --show-toplevel)"
BLOCK_TESTS="${BLOCK_TESTS:-$REPO/build/opstack-executor/tests/opstack-executor-block-tests}"
VECTORS_DIR="$REPO/opstack-executor/tests/t8n/vectors"

# 前置：向量 json 必须存在（.gitignore'd + CI 现场生成；manifest.txt 是跟踪的，不能当哨兵）
if ! ls "$VECTORS_DIR"/*.json >/dev/null 2>&1; then
  echo "ERROR: t8n 向量 json 缺失（需 Go 工具链 + op-geth@pin 克隆）。先跑:" >&2
  echo "  bash opstack-executor/tests/t8n/generator/ensure-vectors.sh" >&2
  exit 2
fi
if [ ! -x "$BLOCK_TESTS" ]; then
  echo "ERROR: 测试二进制缺失。先构建: ninja -C build opstack-executor-block-tests" >&2
  exit 2
fi

echo "== 跑 opstack-executor-block-tests（整二进制 = CI gate，含 Path A/B）=="
rc=0
OUT="$("$BLOCK_TESTS" 2>&1)" || rc=$?    # 捕获退出码（主信号），不 set -e 中断
echo "$OUT"

# 汇总（诊断辅助；成败以 rc 为准）
KNOWN=$(echo "$OUT" | grep -c 'KNOWN-DIVERGE' || true)
SUMMARY=$(echo "$OUT" | grep 'single-path summary:' || echo "single-path summary: <absent>")

if [ "${rc}" -eq 0 ]; then
  echo "PASS 全绿 exempt=$KNOWN | $SUMMARY"
  exit 0
fi
# 失败/崩溃区分（rc 免疫输出格式漂移）；先判 200（setup 错误），再判 >=128（信号崩溃）
if [ "${rc}" -eq 200 ]; then
  echo "FAIL 测试 setup 错误（filter 无匹配等）rc=200; $SUMMARY" >&2
elif [ "${rc}" -ge 128 ]; then
  echo "FAIL 测试二进制崩溃 signal=$((rc-128)); exempt=$KNOWN | $SUMMARY" >&2
  echo "$OUT" | grep -E 'Segmentation fault|Abort|Signal' | head -5 >&2 || true
else
  echo "FAIL 有测试失败 rc=${rc}; exempt=$KNOWN | $SUMMARY" >&2
  echo "$OUT" | grep -E 'error: in|failures are detected' | head -20 >&2 || true
fi
exit 1
