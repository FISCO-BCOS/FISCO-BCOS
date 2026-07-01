#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Gate G4: ERC-20 deposit roundtrip (L1 -> L2 and back). This is the
# end-to-end bridge acceptance gate.
#
# ACCEPTANCE BLOCKED ON A8: a deposit roundtrip needs an L1 chain, the deployed
# L1 bridge/portal contracts, a funded L1 key, and the op-node relaying deposit
# events to L2 — all of which the A8 devnet (tools/.ci/l2-devnet) provides. None
# of it exists yet, so this gate is a SKELETON: the steps are enumerated as
# comments and the script SKIPs (exit 77). It does NOT fake a pass.
#
# Full procedure once A8 lands (each step gets a real assertion):
#   1.  Deploy / read the L1 ERC-20 test token; mint to the L1 test account.
#   2.  approve(L1StandardBridge, amount) on the L1 token.
#   3.  L1StandardBridge.depositERC20(l1Token, l2Token, amount, minGasLimit, ..)
#       from the funded L1 key; capture the L1 tx receipt.
#   4.  Wait for op-node to relay the deposit; poll the L2
#       OptimismMintableERC20 balanceOf(recipient) until it == amount.
#   5.  Assert the L2 balance increased by exactly `amount`.
#   6.  L2StandardBridge.withdraw(l2Token, amount, minGasLimit, ..) on L2.
#   7.  Drive the withdrawal proof + finalize on L1 (challenge window).
#   8.  Assert the L1 token balance is restored and the L2 balance is 0.
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# Even with the devnet compose present, the L1 + bridge tooling is an A8
# deliverable. Require it explicitly so this never silently "passes".
require_devnet

# The L1 bridge tooling marker that A8 will provide. Until it exists, SKIP.
L1_BRIDGE_TOOLING="${L1_BRIDGE_TOOLING:-${REPO_ROOT}/tools/.ci/l2-devnet/l1-bridge/deploy.json}"
if [[ ! -f "${L1_BRIDGE_TOOLING}" ]]; then
    skip "ERC-20 deposit roundtrip requires A8 L1 + bridge tooling (${L1_BRIDGE_TOOLING})"
fi

# If we reach here the A8 tooling exists; the numbered steps above become real
# assertions. Until that tooling lands this branch is unreachable, so fail
# loudly rather than pretend — implementing the steps is the A8 follow-up.
fail "G4 deposit roundtrip steps not yet implemented (A8 tooling detected but driver is TODO-A8)"
