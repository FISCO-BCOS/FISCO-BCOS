#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Gate G3: op-node drives block production. With the A8 devnet running an
# op-node against the FISCO-BCOS L2 execution engine, the chain must keep
# producing blocks: eth_blockNumber must strictly increase over a poll window.
#
# ACCEPTANCE BLOCKED ON A8: this gate cannot pass until the A8 devnet
# (tools/.ci/l2-devnet) provides the op-node + execution-engine wiring. Until
# then it SKIPs (exit 77).
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# Poll parameters: require the block number to advance by at least MIN_ADVANCE
# within POLL_ATTEMPTS one-second polls.
POLL_ATTEMPTS="${POLL_ATTEMPTS:-30}"
MIN_ADVANCE="${MIN_ADVANCE:-3}"

require_devnet
require_cmd curl jq
wait_for_rpc

hex_to_dec() { printf '%d' "$1"; }

start_hex="$(rpc_result eth_blockNumber)"
start="$(hex_to_dec "${start_hex}")"
log "start block = ${start} (${start_hex})"

i=0
current="${start}"
while [[ "${i}" -lt "${POLL_ATTEMPTS}" ]]; do
    sleep 1
    cur_hex="$(rpc_result eth_blockNumber)"
    current="$(hex_to_dec "${cur_hex}")"
    advanced=$(( current - start ))
    if [[ "${advanced}" -ge "${MIN_ADVANCE}" ]]; then
        log "block advanced by ${advanced} (${start} -> ${current}) within $(( i + 1 ))s"
        log "PASS (op-node is driving block production)"
        exit 0
    fi
    i=$(( i + 1 ))
done

fail "block number did not advance by ${MIN_ADVANCE} within ${POLL_ATTEMPTS}s (start=${start} end=${current}); op-node not producing"
