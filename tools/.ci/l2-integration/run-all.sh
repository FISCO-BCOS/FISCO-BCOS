#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Run every L2 integration scenario, tallying PASS / FAIL / SKIPPED.
#
# Per-scenario exit-code contract (see lib.sh):
#   0   -> PASS
#   77  -> SKIPPED (devnet absent, or a sub-step blocked on A8 tooling)
#   *   -> FAIL
#
# Final line:
#   - all non-skipped scenarios passed, at least one ran ->
#       "ALL <n> INTEGRATION TESTS PASSED" (exit 0)
#   - every scenario skipped -> "ALL SKIPPED (devnet absent)" (exit 0)
#   - any scenario failed -> non-zero exit
#
# REQUIRE_DEVNET=1 turns the all-skipped case into a failure. The lenient default is for the
# callers that have no A8 devnet by design (CI's "L2 integration runner (SKIPs without the A8
# devnet)" step), where a run that measures nothing is expected; a caller that DOES provision the
# devnet should set this so a broken provisioning step cannot leave the gate green.
set -uo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_EXIT=77
REQUIRE_DEVNET="${REQUIRE_DEVNET:-0}"

# Ordered scenario list. Gates run last.
SCENARIOS=(
    genesis-bootstrap.sh
    system-config-roundtrip.sh
    disabled-precompile-call.sh
    proxy-upgrade.sh
    extcodehash-extcodecopy.sh
    kzg-precompile-call.sh
    gate-g3.sh
    gate-g4.sh
)

pass=0
fail=0
skip=0
declare -a failed_names=()
declare -a skipped_names=()

for s in "${SCENARIOS[@]}"; do
    path="${DIR}/${s}"
    echo "================================================================"
    echo "RUN: ${s}"
    echo "----------------------------------------------------------------"
    if [[ ! -x "${path}" ]]; then
        # Source-able even if the +x bit was lost in checkout.
        bash "${path}"
    else
        "${path}"
    fi
    rc=$?
    if [[ "${rc}" -eq 0 ]]; then
        echo "RESULT: PASS (${s})"
        pass=$(( pass + 1 ))
    elif [[ "${rc}" -eq "${SKIP_EXIT}" ]]; then
        echo "RESULT: SKIPPED (${s})"
        skip=$(( skip + 1 ))
        skipped_names+=("${s}")
    else
        echo "RESULT: FAIL (${s}, exit ${rc})"
        fail=$(( fail + 1 ))
        failed_names+=("${s}")
    fi
done

total=${#SCENARIOS[@]}
ran=$(( pass + fail ))
echo "================================================================"
echo "SUMMARY: ${total} scenarios  |  PASS=${pass}  FAIL=${fail}  SKIPPED=${skip}"
[[ "${#skipped_names[@]}" -gt 0 ]] && echo "  skipped: ${skipped_names[*]}"
[[ "${#failed_names[@]}" -gt 0 ]]  && echo "  failed:  ${failed_names[*]}"

if [[ "${fail}" -gt 0 ]]; then
    echo "INTEGRATION TESTS FAILED (${fail} of ${total})"
    exit 1
fi
if [[ "${ran}" -eq 0 ]]; then
    echo "ALL SKIPPED (devnet absent)"
    if [[ "${REQUIRE_DEVNET}" == "1" ]]; then
        echo "REQUIRE_DEVNET=1: every scenario skipped, so nothing was measured"
        exit 1
    fi
    exit 0
fi
echo "ALL ${pass} INTEGRATION TESTS PASSED"
exit 0
