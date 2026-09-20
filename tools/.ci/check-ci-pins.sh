#!/usr/bin/env bash
# =============================================================================
# WI-32 gates 2/4, 4/4 and the registry-citation convention check.
#
# F-A4 — TWO INDEPENDENT PIN AXES (the audit conflated them; do not re-conflate):
#
#   * op-geth pin (AUTHORITATIVE): the execution-client revision the reference
#     oracle and the golden samples are generated from. Value
#     e8800cffe53d459cde8a07c8e8f1de9d86e79e07. It must agree across:
#       - <corpus>/opstack-executor/tests/t8n/generator/regen.sh    PIN= line
#       - bcos-evm/test/opstack/op_geth_oracle.json                 source.commit
#       - tools/op-geth-oracle/extract.sh                           OP_GETH_PIN default
#       - opstack-executor/tests/support/GoldenSample.h             header comment
#       - opstack-executor/tests/OpGoldenCorpusProvenanceTest.cpp   c_pinnedOpGethCommit
#     The last one is an extra guard (same axis) so the C++ constant cannot drift
#     away from the JSON contract silently.
#
#   * corpus-repo ref: the FISCO-BCOS/op-stack-e2e-tests commit whose generator
#     CI runs. Currently b82691ac133a94637da9e6969a63f86279c6b0a2. This is a
#     DIFFERENT axis, NOT an op-geth pin. It must agree across the workflow.yml
#     checkout/action refs, tools/.ci/provision_t8n_corpus.sh's default and the
#     corpus's own opstack-executor/tests/t8n/.t8n-pin.
#
#   * C2 harness ref (Axis 4): the op-stack-e2e-tests commit the C2 jobs check
#     out to .ci-op-e2e-tests. Same repository as the corpus ref, deliberately a
#     different commit — it supplies the C2 scripts and the versions.json whose
#     op_monorepo.commit the jobs fetch from ethereum-optimism/optimism. The two
#     C2 entry points (workflow.yml's c2_e2e job and c2-e2e.yml) must pin the
#     same ref; when it equals the corpus ref, the versions.json at that ref is
#     checked for the historical op-revm-revision confusion.
#
# op-revm oracle: bcos-evm/test/opstack/op_revm_oracle.json source.revision must
# equal the revision the audit documents state (see EXPECTED_OP_REVM_REVISION).
#
# Corpus artifacts: the getPayload goldens and the matrix/ contract files must be
# present AT THE PINNED REF. The current corpus ref b82691ac133a94637da9e6969a63f86279c6b0a2
# carries them; if a future ref bump regresses to an older generator, this script
# FAILS FAST, naming the missing artifacts, the corpus ref and the places to bump
# (under GITHUB_ACTIONS CMake auto-defines FISCO_REQUIRE_T8N_CORPUS, so a stale pin
# hard-fails OpGoldenCorpusProvenanceTest's getPayload case while the matrix test
# degrades to "artifacts absent; skipping"). See the blocking note in workflow.yml.
#
# Registry citations (convention): every `specs/...:<line>` citation in
# opstack-executor/tests/da-matrix/DIVERGENCES.md must resolve via
# `git -C <specs> show <pin>:<path>`. When SPECS_DIR is supplied the check is
# enforced; otherwise it degrades to a ::notice:: (the known-bad citation that
# the registry itself documents as non-existent is allowlisted below).
#
# Env:
#   CORPUS_DIR   op-stack-e2e-tests checkout at the CI corpus ref (required)
#   SPECS_DIR    ethereum-optimism/specs checkout at SPECS_PIN (optional)
#   SPECS_PIN    specs revision the citations are resolved against
#   REPO_ROOT    default: `git rev-parse --show-toplevel`
# Exit: 0 only when every axis agrees and every required corpus artifact exists.
# =============================================================================
set -uo pipefail

REPO_ROOT="${REPO_ROOT:-$(git rev-parse --show-toplevel)}"
CORPUS_DIR="${CORPUS_DIR:-}"
# The specs revision the DIVERGENCES.md citations are written against
# (`specs/protocol/karst/overview.md:20` was checked at 564a0ce).
SPECS_PIN="${SPECS_PIN:-564a0ceae302eaf465edc7ff8ab55850624a11a0}"
WF="$REPO_ROOT/.github/workflows/workflow.yml"
# The fork-matrix schedules pin the corpus ref too; Axis 3 covers them.
WF_NIGHTLY="$REPO_ROOT/.github/workflows/opstack-fork-nightly.yml"
WF_WEEKLY="$REPO_ROOT/.github/workflows/opstack-fork-weekly.yml"
DIVERGENCES="$REPO_ROOT/opstack-executor/tests/da-matrix/DIVERGENCES.md"

# The audit's stated op-revm revision (the audit notes are local-only per .gitignore —
# docs/2*-*.md, docs/plans/ — so EXPECTED_OP_REVM_REVISION below is the operative record;
# the doc:line citations are provenance for whoever has the archive, not a tree path):
#   docs/2026-09-12-opstack-fork-allforks-delta-audit.md:13  "op-revm ... @ 5f90f749ca"
#   docs/plans/2026-09-12-plan-e-spike-notes.md:8            "5f90f749caea14398554afb75062f7111b1fc554"
# The value is pinned here: when the doc is present in the tree it is cross-checked
# too; when docs/ is not committed the doc axis is skipped (::notice:: below).
readonly EXPECTED_OP_REVM_REVISION="5f90f749caea14398554afb75062f7111b1fc554"
readonly OP_REVM_DOC="docs/2026-09-12-opstack-fork-allforks-delta-audit.md"

# Citations the registry itself documents as non-existent (DIVERGENCES.md,
# `eip7825_deposit_exemption`): the entry quotes `specs/protocol/karst/overview.md:20`
# precisely to record that it was a fabricated anchor. Excluded here so the gate
# enforces the rule without re-failing on the negative evidence.
readonly CITATION_ALLOWLIST="specs/protocol/karst/overview.md:20"

rc=0
fail()
{
    echo "::error::$*" >&2
    rc=1
}

# Print "<path> <ref>" for every checkout of the corpus repo in $1. YAML comments
# are stripped first: the surrounding prose names hashes (Axis 4 documents
# b82691ac, 5f90f749c) and those must not be parsed as the pin.
checkout_pins() {
    sed 's/#.*//' "$1" | awk '
        /repository:[[:space:]]*FISCO-BCOS\/op-stack-e2e-tests/ {
            look = 1; ref = ""; path = ""; next
        }
        # Block boundary: a new step or another repository: line ends the scan, so
        # a checkout that names no ref cannot be paired with a later step ref.
        look && /^[[:space:]]*-/ { look = 0 }
        look && /^[[:space:]]*(uses|repository):/ { look = 0 }
        look && match($0, /ref:[[:space:]]*[0-9a-zA-Z._\/-]+/) {
            ref = substr($0, RSTART, RLENGTH); sub(/^ref:[[:space:]]*/, "", ref)
        }
        look && match($0, /path:[[:space:]]*[0-9a-zA-Z._\/-]+/) {
            path = substr($0, RSTART, RLENGTH); sub(/^path:[[:space:]]*/, "", path)
        }
        look && path != "" && ref != "" { print path, ref; look = 0 }
    '
}

if [ -z "$CORPUS_DIR" ]; then
    fail "CORPUS_DIR is required (path to the op-stack-e2e-tests checkout at the CI ref)"
    exit 1
fi
if [ ! -d "$CORPUS_DIR" ]; then
    fail "CORPUS_DIR does not exist: $CORPUS_DIR"
    exit 1
fi

# -----------------------------------------------------------------------------
# Axis 1: op-geth pin.
# -----------------------------------------------------------------------------
regen="$CORPUS_DIR/opstack-executor/tests/t8n/generator/regen.sh"
v_regen=""
[ -f "$regen" ] && v_regen="$(sed -nE 's/^PIN="([0-9a-f]{40})".*/\1/p' "$regen" | head -1)"

oracle="$REPO_ROOT/bcos-evm/test/opstack/op_geth_oracle.json"
v_oracle=""
[ -f "$oracle" ] && v_oracle="$(python3 -c \
    'import json,sys;print(json.load(open(sys.argv[1]))["source"]["commit"])' "$oracle" 2>/dev/null)"

extract="$REPO_ROOT/tools/op-geth-oracle/extract.sh"
v_extract=""
[ -f "$extract" ] && v_extract="$(sed -nE \
    's/^PIN="\$\{OP_GETH_PIN:-([0-9a-f]{40})\}".*/\1/p' "$extract" | head -1)"

golden="$REPO_ROOT/opstack-executor/tests/support/GoldenSample.h"
v_golden=""
[ -f "$golden" ] && v_golden="$(grep -A1 'op-geth v1.101702.2' "$golden" \
    | grep -oE '[0-9a-f]{40}' | head -1)"

prov="$REPO_ROOT/opstack-executor/tests/OpGoldenCorpusProvenanceTest.cpp"
v_prov=""
[ -f "$prov" ] && v_prov="$(grep -oE 'c_pinnedOpGethCommit = "[0-9a-f]{40}"' "$prov" \
    | grep -oE '[0-9a-f]{40}' | head -1)"

echo "== op-geth pin axis =="
geth_ref="$v_regen"
[ -n "$geth_ref" ] || geth_ref="$v_oracle"
[ -n "$geth_ref" ] || geth_ref="$v_extract"
[ -n "$geth_ref" ] || geth_ref="$v_golden"
if [ -z "$geth_ref" ]; then
    fail "could not read the op-geth pin from any source"
else
    echo "reference (regen.sh when present): $geth_ref"
fi
for entry in \
    "regen.sh:$regen:$v_regen" \
    "op_geth_oracle.json:$oracle:$v_oracle" \
    "tools/op-geth-oracle/extract.sh:$extract:$v_extract" \
    "GoldenSample.h:$golden:$v_golden" \
    "OpGoldenCorpusProvenanceTest.cpp:$prov:$v_prov"; do
    name="${entry%%:*}"; rest="${entry#*:}"; path="${rest%%:*}"; value="${rest#*:}"
    if [ -z "$value" ]; then
        fail "$name: no op-geth pin found in $path"
    elif [ -n "$geth_ref" ] && [ "$value" != "$geth_ref" ]; then
        fail "$name disagrees: $value != $geth_ref ($path)"
    else
        echo "  OK $name = $value"
    fi
done

# -----------------------------------------------------------------------------
# Axis 2: op-revm oracle revision vs the documented revision.
# -----------------------------------------------------------------------------
revm_oracle="$REPO_ROOT/bcos-evm/test/opstack/op_revm_oracle.json"
v_revm=""
[ -f "$revm_oracle" ] && v_revm="$(python3 -c \
    'import json,sys;print(json.load(open(sys.argv[1]))["source"]["revision"])' "$revm_oracle" 2>/dev/null)"

echo "== op-revm revision axis =="
if [ -z "$v_revm" ]; then
    fail "op_revm_oracle.json: source.revision missing ($revm_oracle)"
elif [ "$v_revm" != "$EXPECTED_OP_REVM_REVISION" ]; then
    fail "op_revm_oracle.json source.revision disagrees with the documented revision: \
$v_revm != $EXPECTED_OP_REVM_REVISION"
else
    echo "  OK op_revm_oracle.json source.revision = $v_revm"
fi
if [ -f "$REPO_ROOT/$OP_REVM_DOC" ]; then
    if grep -q "${EXPECTED_OP_REVM_REVISION:0:10}" "$REPO_ROOT/$OP_REVM_DOC"; then
        echo "  OK $OP_REVM_DOC cites ${EXPECTED_OP_REVM_REVISION:0:10}"
    else
        fail "$OP_REVM_DOC no longer cites ${EXPECTED_OP_REVM_REVISION:0:10} — \
update EXPECTED_OP_REVM_REVISION in $0 together with the oracle"
    fi
else
    echo "  ::notice::$OP_REVM_DOC not present in the tree; skipping the docs \
cross-check axis, the op-revm revision is checked against the embedded pin only"
fi

# -----------------------------------------------------------------------------
# Axis 3 (F-A4): corpus-repo ref agreement across workflow.yml and the
# opstack-fork-nightly/weekly fork-matrix pins.
# -----------------------------------------------------------------------------
wf_refs="$(for f in "$WF" "$WF_NIGHTLY" "$WF_WEEKLY"; do checkout_pins "$f"; done \
    | awk '$1 != ".ci-op-e2e-tests" { print $2 }' | sort -u)"
action_refs="$(grep -hoE 'opstack-t8n-regen@[0-9a-f]{40}' \
    "$WF" "$WF_NIGHTLY" "$WF_WEEKLY" | sed 's/.*@//' | sort -u)"
provision="$REPO_ROOT/tools/.ci/provision_t8n_corpus.sh"
v_provision=""
[ -f "$provision" ] && v_provision="$(sed -nE \
    's/^PIN="\$\{T8N_CORPUS_PIN:-([0-9a-f]{40})\}".*/\1/p' "$provision" | head -1)"
v_pinfile=""
pinfile="$CORPUS_DIR/opstack-executor/tests/t8n/.t8n-pin"
[ -f "$pinfile" ] && v_pinfile="$(tr -d '[:space:]' < "$pinfile")"
v_corpus_head="$(git -C "$CORPUS_DIR" rev-parse HEAD 2>/dev/null)"

echo "== corpus-repo ref axis (F-A4: distinct from the op-geth pin) =="
corpus_ref=""
for r in $wf_refs $action_refs; do
    if [ -z "$corpus_ref" ]; then
        corpus_ref="$r"
    elif [ "$r" != "$corpus_ref" ]; then
        fail "workflow pins disagreeing corpus refs: $r != $corpus_ref"
    fi
done
if [ -z "$corpus_ref" ]; then
    fail "no corpus ref found in $WF, $WF_NIGHTLY or $WF_WEEKLY"
else
    echo "reference (workflow pins): $corpus_ref"
    echo "  workflow checkout refs = $(echo $wf_refs | tr '\n' ' ')"
    echo "  workflow action refs   = $(echo $action_refs | tr '\n' ' ')"
fi
# Per-file assertion (corpus context only: the e2e-tests checkout ref and the
# opstack-t8n-regen action ref — other 40-hex refs in these files are unrelated).
if [ -n "$corpus_ref" ]; then
    for wfile in "$WF" "$WF_NIGHTLY" "$WF_WEEKLY"; do
        wname="${wfile##*/}"
        if grep -qE "ref: $corpus_ref" "$wfile" \
            && grep -qE "opstack-t8n-regen@$corpus_ref" "$wfile"; then
            echo "  OK $wname corpus pins = $corpus_ref (checkout + regen action)"
        else
            fail "$wname: missing or mismatched corpus-context ref (expected $corpus_ref)"
        fi
    done
fi
for entry in \
    "tools/.ci/provision_t8n_corpus.sh (T8N_CORPUS_PIN default):$v_provision" \
    "corpus .t8n-pin:$v_pinfile" \
    "corpus checkout HEAD:$v_corpus_head"; do
    name="${entry%:*}"; value="${entry##*:}"
    if [ "$name" = "corpus .t8n-pin" ]; then
        # The corpus tracks this marker itself and does not necessarily advance it
        # on every commit (it still read 759a9af at corpus HEAD 5e8213c), so a
        # mismatch is reported but must not block the intended ref bump.
        if [ -z "$value" ]; then
            echo "  ::notice::corpus .t8n-pin absent at this ref (added after 759a9af); \
once bumped it should equal $corpus_ref"
        elif [ -n "$corpus_ref" ] && [ "$value" != "$corpus_ref" ]; then
            echo "  ::warning::corpus .t8n-pin = $value lags the CI ref $corpus_ref \
(corpus-side marker; report only)"
        else
            echo "  OK corpus .t8n-pin = $value"
        fi
        continue
    fi
    if [ -z "$value" ]; then
        fail "$name: empty"
    elif [ -n "$corpus_ref" ] && [ "$value" != "$corpus_ref" ]; then
        fail "$name disagrees: $value != $corpus_ref"
    else
        echo "  OK $name = $value"
    fi
done

# -----------------------------------------------------------------------------
# Axis 4: the C2 harness ref — the third pin axis, inside the SAME repository as
# the corpus ref but a different commit: it supplies the C2 scripts AND
# tools/op-e2e/versions.json, whose op_monorepo.commit the C2 jobs check out of
# ethereum-optimism/optimism. Nothing about that file is checked when only the
# corpus ref is vetted, which is how workflow.yml came to carry the corpus ref
# b82691ac in its C2 checkout — a valid harness commit, but an old one whose
# versions.json still read op_monorepo.commit = 5f90f749c (the op-revm revision,
# absent from the monorepo), so the job died in the monorepo checkout, on "not our ref".
# Both C2 entry points must therefore pin the same harness ref, and whenever that
# ref is the corpus ref itself the versions.json it carries is asserted sane here.
# -----------------------------------------------------------------------------
WF_C2="$REPO_ROOT/.github/workflows/c2-e2e.yml"

echo "== C2 harness ref axis (same repo as the corpus ref, different commit) =="
c2_ref=""
if [ ! -f "$WF_C2" ]; then
    fail "c2-e2e.yml not found under $REPO_ROOT/.github/workflows"
else
    for entry in "workflow.yml:$WF" "c2-e2e.yml:$WF_C2"; do
        wname="${entry%%:*}"; wfile="${entry#*:}"
        refs="$(checkout_pins "$wfile" | awk '$1 == ".ci-op-e2e-tests" { print $2 }' | sort -u)"
        count="$(echo "$refs" | grep -c . || true)"
        if [ "$count" -eq 0 ]; then
            fail "$wname: no .ci-op-e2e-tests harness checkout found"
            continue
        fi
        if [ "$count" -ne 1 ]; then
            fail "$wname: $count .ci-op-e2e-tests harness refs ($(echo $refs | tr '\n' ' ')); the C2 harness must be pinned exactly once"
            continue
        fi
        echo "  $wname harness ref = $refs"
        if [ -z "$c2_ref" ]; then
            c2_ref="$refs"
        elif [ "$refs" != "$c2_ref" ]; then
            fail "C2 harness refs disagree: $wname pins $refs, the other entry point pins $c2_ref — bump both together (this divergence is how PR #5615's C2 job got a stale versions.json)"
        fi
    done
fi

# versions.json is only consumed by C2 when the harness ref IS the corpus ref
# (otherwise C2 reads the file at its own ref, which this checkout cannot see —
# the workflow's reachability step vets that value on the network).
v_versions_file="$CORPUS_DIR/tools/op-e2e/versions.json"
if [ -n "$c2_ref" ] && [ "$c2_ref" = "$corpus_ref" ] && [ -f "$v_versions_file" ]; then
    monorepo_pin="$(sed -n '/"op_monorepo"/,/}/p' "$v_versions_file" \
        | sed -nE 's/.*"commit"[[:space:]]*:[[:space:]]*"([0-9a-zA-Z]+)".*/\1/p' | head -1)"
    case "$monorepo_pin" in
        "") fail "versions.json at $c2_ref: no op_monorepo.commit found" ;;
        "$EXPECTED_OP_REVM_REVISION")
            fail "versions.json at $c2_ref pins op_monorepo.commit = $monorepo_pin, which is the op-revm oracle revision — that object does not exist in ethereum-optimism/optimism and the C2 checkout dies on 'not our ref'; bump the C2 harness ref past the versions.json fix"
            ;;
        *)
            if [ "${#monorepo_pin}" -eq 40 ]; then
                echo "  OK versions.json at the shared ref pins op_monorepo.commit = $monorepo_pin"
            else
                fail "versions.json at $c2_ref: op_monorepo.commit '$monorepo_pin' is not a 40-hex sha"
            fi
            ;;
    esac
elif [ -f "$v_versions_file" ]; then
    monorepo_pin="$(sed -n '/"op_monorepo"/,/}/p' "$v_versions_file" \
        | sed -nE 's/.*"commit"[[:space:]]*:[[:space:]]*"([0-9a-zA-Z]+)".*/\1/p' | head -1)"
    if [ "$monorepo_pin" = "$EXPECTED_OP_REVM_REVISION" ]; then
        echo "  ::notice::corpus-ref versions.json still pins op_monorepo.commit = $monorepo_pin (the op-revm revision); harmless while C2 uses its own harness ref $c2_ref, but do not point C2 here"
    fi
fi

# -----------------------------------------------------------------------------
# Axis 3 continuation: the pinned corpus ref must carry the getPayload + matrix
# artifacts the tests now require in CI.
# -----------------------------------------------------------------------------
echo "== corpus artifacts at the CI ref =="
missing=""
for rel in \
    opstack-executor/tests/t8n/golden/engine/getpayload/SHA256SUMS \
    opstack-executor/tests/t8n/golden/engine/getpayload/manifest.txt \
    opstack-executor/tests/t8n/matrix/manifest.txt \
    opstack-executor/tests/t8n/matrix/SHA256SUMS \
    opstack-executor/tests/t8n/matrix/known_deviations.json; do
    if [ -f "$CORPUS_DIR/$rel" ]; then
        echo "  OK $rel"
    else
        echo "  MISSING $rel"
        missing="$missing $rel"
    fi
done
if [ -n "$missing" ]; then
    fail "corpus ref ${corpus_ref:-<unknown>} lacks:$missing
This is the F-A4 CI risk: ${corpus_ref:-<unknown>} predates the getPayload (afe06e2) and
matrix (f1fc9a3) corpus commits, so the regen action runs an older generator. Under
GITHUB_ACTIONS (CMake auto-defines FISCO_REQUIRE_T8N_CORPUS) OpGoldenCorpusProvenanceTest's
getPayload case hard-fails and OpEngineApiMatrixTest degrades to 'artifacts absent; skipping'.
Remedy (corpus must be pushed/merged first): bump the corpus ref in every pin —
workflow.yml's THREE logical places —
  (1) the actions/checkout refs for FISCO-BCOS/op-stack-e2e-tests (build + coverage jobs);
  (2) the build job's  opstack-t8n-regen@<ref>  action ref;
  (3) the coverage job's opstack-t8n-regen@<ref> action ref —
plus the same checkout/action pins in opstack-fork-nightly.yml and opstack-fork-weekly.yml,
tools/.ci/provision_t8n_corpus.sh's default, and the corpus .t8n-pin for local parity.
Do NOT silence this by weakening the provenance/matrix tests."
fi

# -----------------------------------------------------------------------------
# Registry citations: every `specs/...:<line>` must resolve at the specs pin.
# -----------------------------------------------------------------------------
echo "== registry citation reproducibility =="
if [ ! -f "$DIVERGENCES" ]; then
    fail "missing registry: $DIVERGENCES"
else
    cites="$(grep -oE 'specs/[A-Za-z0-9._/-]+:[0-9]+' "$DIVERGENCES" | sort -u)"
    if [ -z "$cites" ]; then
        echo "  no specs/...:line citations found"
    fi
    for cite in $cites; do
        if [ "$cite" = "$CITATION_ALLOWLIST" ]; then
            echo "  SKIP $cite (registry documents this citation as non-existent)"
            continue
        fi
        if [ -z "${SPECS_DIR:-}" ]; then
            echo "  ::notice::unverified $cite (SPECS_DIR not set; supply a specs checkout to enforce)"
            continue
        fi
        path="${cite%:*}"; line="${cite##*:}"
        if ! git -C "$SPECS_DIR" cat-file -e "$SPECS_PIN:$path" 2>/dev/null; then
            fail "citation $cite does not resolve at specs $SPECS_PIN (path missing: $path)"
            continue
        fi
        nlines="$(git -C "$SPECS_DIR" show "$SPECS_PIN:$path" 2>/dev/null | wc -l | tr -d ' ')"
        if [ "$line" -gt "$nlines" ]; then
            fail "citation $cite does not resolve at specs $SPECS_PIN \
($path has $nlines lines, line $line does not exist)"
        else
            echo "  OK $cite"
        fi
    done
fi

if [ "$rc" -eq 0 ]; then
    echo "== all pin axes agree =="
fi
exit $rc
