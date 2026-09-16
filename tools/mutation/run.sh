#!/usr/bin/env bash
# =============================================================================
# M4 fix-reversal mutation harness (P1 Task 8).
#
# Apply each reverse-fix variant, build the mapped target, run the mapped case, and
# REQUIRE it to fail -- a variant that leaves the matrix green is a blind spot, i.e.
# the matrix does not actually guard the fix it claims to.
#
# Red is decided by the process exit code AND the failure marker: Boost prints
# "*** No errors detected" on success, so a bare "errors detected" substring test
# would report every green run as red.
#
# Attribution: every filter in the variant's `also_green` list must stay GREEN. That
# is the negative control -- it shows the mutation killed specifically the mapped
# behaviour and not, say, the whole engine lane. (Boost's --run_test does not accept
# comma-separated filters, so the list is run one entry at a time.)
#
# Usage: run.sh [variant ...]   (default: every variant in mapping.json)
# Exit:  0 only when every selected variant is RED as required and attributed.
# =============================================================================
set -uo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
map="$root/tools/mutation/variants/mapping.json"
# Default file the legacy (N1/N2/NEW-3) variants mutate. Newer variants declare their own
# "mutated" file in the mapping (still used as the legacy single-file default). A variant
# patch may touch MORE than one file (e.g. a production table plus a mirror the compiler
# pins); restore_patch() below derives the file list from the patch itself so a multi-file
# variant cannot leave a dirty tree behind.
mutated="engine/bcos-engine/OpEngineService.inl"

read_ids() { python3 -c "import json;print('\n'.join(v['variant'] for v in json.load(open('$map'))))"; }
field() {
  python3 -c "import json;m=json.load(open('$map'));print(next(str(v.get('$2','')) for v in m if v['variant']=='$1'))"
}
must_stay_green() {
  python3 -c "import json;m=json.load(open('$map'));print('\n'.join(next(v.get('also_green',[]) for v in m if v['variant']=='$1')))"
}

ids=("$@")
if [ "${#ids[@]}" -eq 0 ]; then
  # Portable read loop: stock macOS bash 3.2 has no mapfile.
  while IFS= read -r id; do [ -n "$id" ] && ids+=("$id"); done < <(read_ids)
fi
if [ "${#ids[@]}" -eq 0 ]; then echo "no variants selected (empty mapping?)" >&2; exit 1; fi

is_red() {  # $1 = output, $2 = exit code
  [ "$2" -ne 0 ] && return 0
  echo "$1" | grep -qE '\*\*\* [0-9]+ failures? (is|are) detected|\*\*\* Errors were detected'
}

# NOTE: `git apply --3way` implies --index, so it stages the mutation as well as
# writing the worktree. `git checkout -- <path>` would restore the worktree FROM that
# mutated index and silently leave the variant in place (accumulating across runs);
# resetting to HEAD covers both the index and the worktree.
restore() { git -C "$root" checkout HEAD -- "$1" 2>/dev/null || true; }

# Every path a variant patch touches. `git apply --numstat` reads the patch (not the tree),
# so this works before or after applying it.
patch_files() { git -C "$root" apply --numstat "$1" 2>/dev/null | awk '{print $3}'; }
restore_patch() {
  local f
  while IFS= read -r f; do
    [ -n "$f" ] && restore "$f"
  done < <(patch_files "$1")
}

rc_all=0
for id in "${ids[@]}"; do
  patch="$root/tools/mutation/variants/$id.patch"
  [ -f "$patch" ] || { echo "[$id] NO VARIANT PATCH ($patch)"; rc_all=1; continue; }
  target=$(field "$id" target); bin=$(field "$id" binary); filter=$(field "$id" filter)
  # Each variant names the file it mutates; fall back to the legacy default.
  vmutated=$(field "$id" mutated); [ -n "$vmutated" ] || vmutated="$mutated"
  git -C "$root" apply --3way "$patch" || { echo "[$id] APPLY FAILED"; rc_all=1; continue; }
  # Restore on ANY exit (interrupt included) so the tree never stays patched.
  trap 'restore_patch "$patch"' EXIT INT TERM

  if ! ninja -C "$root/build" "$target" >/dev/null 2>&1; then
    echo "[$id] BUILD FAILED under the variant (variant is unusable)"; rc_all=1
    restore_patch "$patch"; trap - EXIT INT TERM; continue
  fi

  out="$("$root/build/$bin" "--run_test=$filter" 2>&1)"; code=$?
  if is_red "$out" "$code"; then
    echo "[$id] RED as required ($filter)"
  else
    echo "[$id] STILL GREEN -- matrix blind spot ($filter)"; rc_all=1
  fi

  while IFS= read -r other; do
    [ -n "$other" ] || continue
    out2="$("$root/build/$bin" "--run_test=$other" 2>&1)"; code2=$?
    if is_red "$out2" "$code2"; then
      echo "[$id] NOT ATTRIBUTED -- negative control also failed ($other)"; rc_all=1
    fi
  done < <(must_stay_green "$id")

  restore_patch "$patch"; trap - EXIT INT TERM
done

# Cleanliness gate covers EVERY mutated file in the mapping (legacy default included), so a
# silently failed per-variant restore cannot leave a mutation behind.
while IFS= read -r f; do
  [ -n "$f" ] || continue
  if [ -n "$(git -C "$root" status --porcelain -- "$f")" ]; then
    echo "variant file still modified after run: $f" >&2; rc_all=1
  fi
done < <({ python3 -c "import json;print('\n'.join(sorted({v.get('mutated','') or '$mutated' for v in json.load(open('$map'))})))"; for pf in "$root"/tools/mutation/variants/*.patch; do patch_files "$pf"; done; } | sort -u)
exit $rc_all
