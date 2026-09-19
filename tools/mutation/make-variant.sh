#!/usr/bin/env bash
# =============================================================================
# Author a new fix-reversal variant patch (P1 Task 8).
#
# The variants are hand-written MINIMAL reverses of one judgement each, not
# `git apply -R` of a whole fix commit: the fix commits in this area are large and
# several touch the same regions, so a full reverse either fails to apply or reverts
# findings the variant is not about (measured: only 4a59505a1 reversed cleanly).
#
# Workflow:
#   1. edit engine/bcos-engine/OpEngineService.inl by hand, reversing ONE judgement
#   2. run this script with the variant id -> writes variants/<ID>.patch, restores HEAD
#   3. add the variant to variants/mapping.json (target/filter/also_green)
#   4. bash tools/mutation/run.sh <ID>   -> must be RED and attributed
#
# Usage: make-variant.sh <ID> [mutated-file]
# =============================================================================
set -euo pipefail

[ "$#" -ge 1 ] || { echo "usage: make-variant.sh <ID> [mutated-file]" >&2; exit 2; }
id="$1"
root="$(cd "$(dirname "$0")/../.." && pwd)"
mutated="${2:-engine/bcos-engine/OpEngineService.inl}"
out="$root/tools/mutation/variants/$id.patch"

if [ -z "$(git -C "$root" status --porcelain -- "$mutated")" ]; then
  echo "nothing edited in $mutated -- reverse a judgement first" >&2
  exit 1
fi

git -C "$root" diff -- "$mutated" > "$out"
# Reset both index and worktree: `git diff` here is worktree-vs-index, and a stray
# `git add` would otherwise leave the mutation staged.
git -C "$root" checkout HEAD -- "$mutated"
echo "wrote $out ($(wc -l < "$out" | tr -d ' ') lines); $mutated restored to HEAD"
echo "now add $id to tools/mutation/variants/mapping.json and run tools/mutation/run.sh $id"
