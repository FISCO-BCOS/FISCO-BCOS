#!/usr/bin/env bash
# Phase-4 regression gate: bring up B3 (passive) + B3a (active) and run every op-e2e script,
# failing the gate if any exits non-zero. Requires the sign_secp helper and the b3a active
# instance (cloned from B3, ports 8563/8564, empty chain).
set -u
cd "$(dirname "$0")"

fail=0
step() { echo "==== $1 ===="; }

step "restart B3 (passive, storage preserved)"
bash restart_b3.sh || fail=1

step "A: rpc_matrix (51 asserts)"
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
python3 predeploy_matrix.py || fail=1

step "A.1+B4: a1_active on B3a (16 asserts)"
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
