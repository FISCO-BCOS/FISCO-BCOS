#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Negative control for the kona parity gate: repoint the OP base fee vault in the SOURCE tree so
# the fisco-bcos built afterwards credits the base fee to 0x42…00ff instead of 0x42…0019.
#
# Why a source patch and not a genesis edit: kona-host reads FISCO's own state through
# debug_dbGet/eth_getProof, so any genesis alloc change is seen identically by both sides and
# cannot diverge. The vault address is compiled in (bcos-evm/bcos-evm/opstack/OpPredeploys.h:16-17)
# and credited per transaction (OpTransition.cpp:342), while kona pays 0x42…0019 — so the first
# block with a fee-paying transaction must diverge in stateRoot.
#
# Usage (CI only; never commit the result): bash mutate-fee-vault.sh [REPO_ROOT]
set -euo pipefail
REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
F="${REPO_ROOT}/bcos-evm/bcos-evm/opstack/OpPredeploys.h"
FROM=0x4200000000000000000000000000000000000019_address
TO=0x42000000000000000000000000000000000000ff_address
[ "$(grep -c "$FROM" "$F")" = 1 ] || { echo "expected exactly one $FROM in $F" >&2; exit 2; }
sed -i.orig "s/$FROM/$TO/" "$F"
rm -f "$F.orig"
grep -q "$TO" "$F" || { echo "substitution did not apply" >&2; exit 2; }
echo "[kona-parity] mutated OP_BASE_FEE_VAULT in $F -> ${TO%_address}"
