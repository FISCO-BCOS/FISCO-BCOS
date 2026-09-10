#!/usr/bin/env bash
# Phase-4 regression gate: bring up B3 (passive) + B3a (active) and run every op-e2e script,
# failing the gate if any exits non-zero. Requires the sign_secp helper and the b3a active
# instance (cloned from B3, ports 8563/8564, empty chain).
set -u

# Tier-2 known-reds (D2 spec v2): B3's single-node push driver is refused by OP-mode
# attribute building (-38003) so the chain sits at genesis — every script that needs
# transactions sealed into blocks fails until OP-mode payload building lands (Tier-2,
# shared prerequisite with C2's sequencer path). They still RUN (output visible) but do
# not fail this gate. Remove names as Tier-2 restores them.
# Tier-2 Phase B complete (08-19): withdrawalsRoot propagation fixed — every script is green,
# full gating restored. The list stays (empty) as the mechanism's documentation.
KNOWN_RED_TIER2=""

run_step() {  # run_step <script-name-without-.py>
    local name="$1"
    if [[ " $KNOWN_RED_TIER2 " == *" $name "* ]]; then
        echo "---- $name.py: [TIER-2 KNOWN RED] ----"
        python3 "$name.py" || echo "  (expected red at Tier-1; gate not affected)"
    else
        python3 "$name.py" || fail=1
    fi
}

cd "$(dirname "$0")"

fail=0
step() { echo "==== $1 ===="; }

step "restart B3 (passive, storage preserved)"
bash restart_b3.sh || fail=1

step "A: rpc_matrix (51 pass + 8 tier-1 known-red)"
python3 rpc_matrix.py || fail=1

step "B.1/B.2: state_verify (12 asserts)"
python3 state_verify.py || fail=1

step "A.2/B.3: chain_driver (31 asserts)"
python3 chain_driver.py || fail=1

step "B.4: b4_persist (3 asserts)"
python3 b4_persist.py || fail=1

step "B.3: b3_contracts (12 asserts)"
python3 b3_contracts.py || fail=1

step "PREDEPLOY: predeploy_matrix (35 asserts)"
run_step predeploy_matrix

step "A.1+B4: a1_active on B3a (11 asserts)"
B3A_START=${B3A_START:-/tmp/op-spike/b3a/start.sh}
bash "$B3A_START" || fail=1
python3 a1_active.py || fail=1

echo
if [ "$fail" -eq 0 ]; then
    echo "ALL OP-E2E GREEN"
else
    echo "OP-E2E FAILED"
    exit 1
fi
