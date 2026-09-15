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
#     CI runs. Currently dc9b945364850668040f6bd91512f9f2c3069b54. This is a
#     DIFFERENT axis, NOT an op-geth pin. It must agree across the workflow.yml
#     checkout/action refs, tools/.ci/provision_t8n_corpus.sh's default and the
#     corpus's own opstack-executor/tests/t8n/.t8n-pin.
#
# op-revm oracle: bcos-evm/test/opstack/op_revm_oracle.json source.revision must
# equal the revision the audit documents state (see EXPECTED_OP_REVM_REVISION).
#
# Corpus artifacts: the getPayload goldens and the matrix/ contract files must be
# present AT THE PINNED REF. They are not (the pin predates the commits that add
# them), and the regen action regenerates from that ref's generator, so under
# GITHUB_ACTIONS (CMake auto-defines FISCO_REQUIRE_T8N_CORPUS) OpGoldenCorpusProvenanceTest's
# getPayload case hard-fails while the matrix test degrades to "artifacts absent;
# skipping". This script therefore FAILS FAST, naming the missing artifacts, the
# corpus ref and the places to bump. See the blocking note in workflow.yml.
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
DIVERGENCES="$REPO_ROOT/opstack-executor/tests/da-matrix/DIVERGENCES.md"

# The audit's stated op-revm revision:
#   docs/2026-09-12-opstack-fork-allforks-delta-audit.md:13  "op-revm ... @ 5f90f749ca"
#   docs/plans/2026-09-12-plan-e-spike-notes.md:8            "5f90f749caea14398554afb75062f7111b1fc554"
# docs/ is not committed, so the value is pinned here; when the doc is present in
# the working tree it is cross-checked too.
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
    echo "  ::notice::$OP_REVM_DOC not present (docs/ is not committed); \
op-revm revision checked against the embedded pin only"
fi

# -----------------------------------------------------------------------------
# Axis 3 (F-A4): corpus-repo ref agreement.
# -----------------------------------------------------------------------------
wf_refs="$(grep -A3 -E 'repository: FISCO-BCOS/op-stack-e2e-tests' "$WF" \
    | grep -oE 'ref: [0-9a-f]{40}' | awk '{print $2}' | sort -u)"
action_refs="$(grep -oE 'opstack-t8n-regen@[0-9a-f]{40}' "$WF" | sed 's/.*@//' | sort -u)"
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
        fail "workflow.yml pins disagreeing corpus refs: $r != $corpus_ref"
    fi
done
if [ -z "$corpus_ref" ]; then
    fail "no corpus ref found in $WF"
else
    echo "reference (workflow.yml): $corpus_ref"
    echo "  workflow checkout refs = $(echo $wf_refs | tr '\n' ' ')"
    echo "  workflow action refs   = $(echo $action_refs | tr '\n' ' ')"
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
This is the F-A4 CI risk: 759a9af0 predates the getPayload (afe06e2) and matrix (f1fc9a3)
corpus commits, so the regen action runs an older generator. Under GITHUB_ACTIONS
(CMake auto-defines FISCO_REQUIRE_T8N_CORPUS) OpGoldenCorpusProvenanceTest's getPayload
case hard-fails and OpEngineApiMatrixTest degrades to 'artifacts absent; skipping'.
Remedy (corpus must be pushed/merged first): bump the corpus ref in the THREE logical
places in .github/workflows/workflow.yml —
  (1) the actions/checkout refs for FISCO-BCOS/op-stack-e2e-tests (build + coverage jobs);
  (2) the build job's  opstack-t8n-regen@<ref>  action ref;
  (3) the coverage job's opstack-t8n-regen@<ref> action ref —
plus tools/.ci/provision_t8n_corpus.sh's default and the corpus .t8n-pin for local parity.
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
