#!/usr/bin/env bash
# =============================================================================
# M4 fix-reversal mutation harness (P1 Task 8).
#
# Apply each reverse-fix variant, build the mapped target, run the mapped case, and
# REQUIRE it to fail -- a variant that leaves the matrix green is a blind spot, i.e.
# the matrix does not actually guard the fix it claims to.
#
# Red requires BOTH a non-zero run AND Boost's failure summary ("*** N failure(s)
# (is|are) detected" / "*** Errors were detected"). A non-zero exit without that
# summary is a configuration error, not a caught mutation: the drift demo (a stale
# filter exits 200 with "Test setup error: no test cases matching filter") used to
# count as RED and hide a dead filter behind an all-green matrix (review finding
# F43). Such runs are reported as HARNESS ERROR and fail the whole run.
#
# Corpus gate: several mapped tests skip themselves when the getpayload/t8n corpus
# is absent, and a skipped mapped test PASSES -- the harness would read that as
# "mutation survived", or worse, report a clean all-green round (review finding
# F49: the two reported "matrix blind spots" were exactly these skips, not real
# gaps). Provision the corpus automatically from MUTATION_CORPUS_DIR or
# ~/.cache/fisco-t8n-corpus (the layout tools/.ci/provision_t8n_corpus.sh
# produces; the link matches CI's workflow.yml step) and refuse to run without it.
#
# Attribution: every filter in the variant's `also_green` list must stay GREEN
# (exit 0) and every filter in its `also_red` list must be RED -- table-value cases
# that read the same mutated literal belong in `also_red` so their reds are
# attributed instead of silently ignored (review finding F44). (Boost's --run_test
# does not accept comma-separated filters, so the lists are run one entry at a
# time.)
#
# Build location: MUTATION_BUILD_DIR overrides the in-tree ./build -- set it when
# the build lives outside the tree (a detached repro worktree with a sibling build
# cannot be reached by the old hardcoded path).
#
# Usage: run.sh [variant ...]   (default: every variant in mapping.json)
# Exit:  0 only when every selected variant is RED as required and attributed.
# =============================================================================
set -uo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${MUTATION_BUILD_DIR:-$root/build}"
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
must_go_red() {
  python3 -c "import json;m=json.load(open('$map'));print('\n'.join(next(v.get('also_red',[]) for v in m if v['variant']=='$1')))"
}

ids=("$@")
if [ "${#ids[@]}" -eq 0 ]; then
  # Portable read loop: stock macOS bash 3.2 has no mapfile.
  while IFS= read -r id; do [ -n "$id" ] && ids+=("$id"); done < <(read_ids)
fi
if [ "${#ids[@]}" -eq 0 ]; then echo "no variants selected (empty mapping?)" >&2; exit 1; fi

# --- corpus gate (F49): a skipped mapped test is counted as a pass by Boost, so the
# harness must not run without the corpus the mapped tests gate themselves on. ---
corpus_src="${MUTATION_CORPUS_DIR:-$HOME/.cache/fisco-t8n-corpus/opstack-executor/tests/t8n}"
corpus_link="$root/opstack-executor/tests/t8n"
corpus_provisioned=""
if [ ! -e "$corpus_link" ]; then
  if [ -d "$corpus_src" ]; then
    ln -s "$corpus_src" "$corpus_link" && corpus_provisioned="$corpus_link"
  else
    echo "harness: no getpayload/t8n corpus at $corpus_link" >&2
    echo "  corpus-gated mapped tests skip themselves when it is absent, and a skipped" >&2
    echo "  mapped test would be counted as a green (silent) run." >&2
    echo "  Provision it first, e.g.: ln -sfn <e2e-tests>/opstack-executor/tests/t8n $corpus_link" >&2
    echo "  or point MUTATION_CORPUS_DIR at the checked-out corpus t8n directory." >&2
    exit 1
  fi
fi
cleanup_corpus() { [ -n "$corpus_provisioned" ] && rm -f "$corpus_provisioned"; return 0; }
trap cleanup_corpus EXIT

# RED | GREEN | CONFIG_ERROR. GREEN only on exit 0 (a survived mutation); RED only on a
# non-zero exit that carries Boost's failure summary; anything else (exit 200 "no test
# cases matching filter", a missing binary, a crash without a summary) is a harness or
# configuration error and fails the run loudly instead of being judged either way (F43).
classify() {  # $1 = output, $2 = exit code
  if [ "$2" -eq 0 ]; then echo GREEN; return; fi
  if echo "$1" | grep -qE '\*\*\* [0-9]+ failures? (is|are) detected|\*\*\* Errors were detected'; then
    echo RED
  else
    echo CONFIG_ERROR
  fi
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

  if ! ninja -C "$build_dir" "$target" >/dev/null 2>&1; then
    echo "[$id] BUILD FAILED under the variant (variant is unusable)"; rc_all=1
    restore_patch "$patch"; trap - EXIT INT TERM; continue
  fi

  out="$("$build_dir/$bin" "--run_test=$filter" 2>&1)"; code=$?
  case "$(classify "$out" "$code")" in
    RED)
      echo "[$id] RED as required ($filter)"
      ;;
    GREEN)
      echo "[$id] STILL GREEN -- matrix blind spot ($filter)"; rc_all=1
      ;;
    CONFIG_ERROR)
      echo "[$id] HARNESS ERROR -- non-zero exit without a Boost failure summary"
      echo "      (filter=$filter exit=$code; drifted filter or setup failure? F43)"
      rc_all=1
      ;;
  esac

  while IFS= read -r other; do
    [ -n "$other" ] || continue
    out2="$("$build_dir/$bin" "--run_test=$other" 2>&1)"; code2=$?
    if [ "$code2" -ne 0 ]; then
      echo "[$id] NOT ATTRIBUTED -- negative control failed ($other, exit=$code2)"; rc_all=1
    fi
  done < <(must_stay_green "$id")

  while IFS= read -r other; do
    [ -n "$other" ] || continue
    out3="$("$build_dir/$bin" "--run_test=$other" 2>&1)"; code3=$?
    if [ "$(classify "$out3" "$code3")" != RED ]; then
      echo "[$id] EXPECTED RED missing in also_red ($other)"; rc_all=1
    fi
  done < <(must_go_red "$id")

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
cleanup_corpus
trap - EXIT
exit $rc_all
