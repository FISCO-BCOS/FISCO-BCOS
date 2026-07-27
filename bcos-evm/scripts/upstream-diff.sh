#!/usr/bin/env bash
# M6 upstream diff guard — compare bcos-evm-ref 照抄面 to pinned evmone REF.
#
# Compares normalized source slices (manifest.tsv) against evmone test/state and
# checks the diff matches checked-in golden patches. Intentional forks (P1 ecrecover,
# OP fee paths, OpHost precompile dispatch) are encoded in golden/*.patch; update
# those when you deliberately change a copy surface.
#
# Usage:
#   ./scripts/upstream-diff.sh                 # CI / local check
#   ./scripts/upstream-diff.sh --show ID       # print live diff for one segment
#   ./scripts/upstream-diff.sh --regenerate-goldens   # refresh golden/*.patch (review + commit)
#
# Environment:
#   EVMONE_GIT   — path to evmone git clone (must contain EVMONE_REF)
#   EVMONE_REF   — override pin (default: scripts/upstream-diff/EVMONE_REF)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UDIR="$ROOT/scripts/upstream-diff"
MANIFEST="$UDIR/manifest.tsv"
NORMALIZE="$UDIR/normalize.sed"
GOLDEN_DIR="$UDIR/golden"
REF_FILE="$UDIR/EVMONE_REF"

REGENERATE=0
SHOW_ID=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --regenerate-goldens) REGENERATE=1; shift ;;
    --show)
      SHOW_ID="${2:-}"
      [[ -n "$SHOW_ID" ]] || {
        echo "usage: $0 --show <segment-id>" >&2
        exit 2
      }
      shift 2
      ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1 (try --help)" >&2
      exit 2
      ;;
  esac
done

EVMONE_REF="${EVMONE_REF:-$(tr -d '[:space:]' < "$REF_FILE")}"

resolve_evmone_git() {
  if [[ -n "${EVMONE_GIT:-}" && -d "$EVMONE_GIT/.git" ]]; then
    echo "$EVMONE_GIT"
    return
  fi
  local candidate
  for candidate in \
    "$HOME/code/blockchain-impl/evmone" \
    "$HOME/octo/code/blockchain-impl/evmone" \
    "$ROOT/../blockchain-impl/evmone" \
    "$ROOT/../../blockchain-impl/evmone"; do
    if [[ -d "$candidate/.git" ]]; then
      echo "$candidate"
      return
    fi
  done
  return 1
}

normalize_stream() {
  sed -E -f "$NORMALIZE"
}

extract_ref() {
  local file="$1" begin="$2" end="$3"
  sed -n "${begin},${end}p" "$ROOT/$file"
}

extract_upstream() {
  local git_dir="$1" up_file="$2" begin="$3" end="$4"
  git -C "$git_dir" show "${EVMONE_REF}:${up_file}" | sed -n "${begin},${end}p"
}

diff_segment() {
  local id="$1" ref_file="$2" ref_begin="$3" ref_end="$4" up_file="$5" up_begin="$6" up_end="$7"
  local tmp_dir ref_norm up_norm patch_now patch_golden

  tmp_dir="$(mktemp -d)"
  ref_norm="$tmp_dir/ref.txt"
  up_norm="$tmp_dir/up.txt"
  patch_now="$tmp_dir/now.patch"

  extract_ref "$ref_file" "$ref_begin" "$ref_end" | normalize_stream >"$ref_norm"
  extract_upstream "$EVMONE_GIT" "$up_file" "$up_begin" "$up_end" | normalize_stream >"$up_norm"

  if diff -w -u --label upstream --label ref "$up_norm" "$ref_norm" >"$patch_now" 2>/dev/null; then
    : >"$patch_now"
  fi

  if [[ "$REGENERATE" -eq 1 ]]; then
    mkdir -p "$GOLDEN_DIR"
    cp "$patch_now" "$GOLDEN_DIR/${id}.patch"
    echo "regenerated golden/$id.patch"
    rm -rf "$tmp_dir"
    return 0
  fi

  if [[ -n "$SHOW_ID" ]]; then
    if [[ "$SHOW_ID" == "$id" ]]; then
      echo "=== $id ($ref_file:$ref_begin-$ref_end ↔ $up_file:$up_begin-$up_end) ==="
      if [[ -s "$patch_now" ]]; then
        cat "$patch_now"
      else
        echo "(no diff after normalization)"
      fi
    fi
    rm -rf "$tmp_dir"
    return 0
  fi

  patch_golden="$GOLDEN_DIR/${id}.patch"
  if [[ ! -f "$patch_golden" ]]; then
    echo "FAIL $id: missing golden/$id.patch — run with --regenerate-goldens" >&2
    rm -rf "$tmp_dir"
    return 1
  fi

  if ! diff -u "$patch_golden" "$patch_now" >/dev/null 2>&1; then
    echo "FAIL $id: drift vs golden (ref $ref_file:$ref_begin-$ref_end)" >&2
    echo "  expected: golden/$id.patch" >&2
    echo "  actual diff:" >&2
    diff -u "$patch_golden" "$patch_now" >&2 || true
    echo "  hint: ./scripts/upstream-diff.sh --show $id" >&2
    rm -rf "$tmp_dir"
    return 1
  fi

  echo "OK   $id"
  rm -rf "$tmp_dir"
  return 0
}

if ! EVMONE_GIT="$(resolve_evmone_git)"; then
  cat >&2 <<EOF
evmone git clone not found.
Set EVMONE_GIT to a clone of https://github.com/ipsilon/evmone containing REF $EVMONE_REF
  export EVMONE_GIT=/path/to/evmone
EOF
  exit 1
fi

if ! git -C "$EVMONE_GIT" cat-file -e "${EVMONE_REF}^{commit}" 2>/dev/null; then
  echo "EVMONE_REF $EVMONE_REF not found in $EVMONE_GIT — fetch tags/commits first" >&2
  exit 1
fi

failures=0
while IFS=$'\t' read -r id ref_file ref_begin ref_end up_file up_begin up_end; do
  [[ -z "$id" || "$id" =~ ^# ]] && continue
  diff_segment "$id" "$ref_file" "$ref_begin" "$ref_end" "$up_file" "$up_begin" "$up_end" || failures=$((failures + 1))
done <"$MANIFEST"

if [[ -n "$SHOW_ID" ]]; then
  exit 0
fi

if [[ "$failures" -gt 0 ]]; then
  echo "$failures segment(s) failed — review diffs or regenerate goldens after intentional fork" >&2
  exit 1
fi

echo "upstream diff: all $(grep -cv '^#\|^$' "$MANIFEST" || true) segments match golden"
