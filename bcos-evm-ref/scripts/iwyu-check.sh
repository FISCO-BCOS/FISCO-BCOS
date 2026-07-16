#!/usr/bin/env bash
# Run IWYU on bcos-evm-ref with project mapping conventions.
# Requires: include-what-you-use (brew), compile_commands.json in build/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
MAP="$ROOT/iwyu-bcos-evm-ref.imp"

if ! command -v include-what-you-use >/dev/null; then
  echo "include-what-you-use not found (brew install include-what-you-use)" >&2
  exit 1
fi

IWYU_TOOL="$(dirname "$(command -v include-what-you-use)")/iwyu_tool.py"
if [[ ! -f "$IWYU_TOOL" ]]; then
  IWYU_TOOL="$(brew --prefix include-what-you-use 2>/dev/null)/bin/iwyu_tool.py"
fi

if [[ ! -f "$BUILD/compile_commands.json" ]]; then
  echo "Generating compile_commands.json in $BUILD ..."
  cmake -S "$ROOT" -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

# IWYU 0.26 on macOS: project flags must use -Xiwyu prefix (clang driver).
exec python3 "$IWYU_TOOL" -j "${JOBS:-6}" -p "$BUILD" "$@" -- \
  -Xiwyu "--mapping_file=$MAP" \
  -Xiwyu --no_comments
