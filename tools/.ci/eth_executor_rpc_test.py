#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# RPC integration test for the pure-Ethereum executor (executor_version=2).
#
# It reuses an EEST (execution-spec-tests) fixture as the "simple Ethereum
# transaction case":
#   * fixtures/state_tests/.../*.json embed a `transaction.secretKey` (the
#     sender's private key) plus `to`/`value`/`gasLimit`, and a `pre` balance
#     for the sender — exactly what a live node needs to run the tx.
#   * The genesis [alloc] pre-funds that sender (see ci_check_eth_executor.sh).
#   * Here we re-sign a fresh legacy value-transfer with the fixture secretKey
#     (chainId = the node's web3_chain_id), submit it via eth_sendRawTransaction,
#     wait for a receipt and assert the transfer landed.
#
# The tx values are passed via CLI (defaults = the fixture case) so the script
# works both in CI (fixtures downloaded) and locally without re-parsing JSON.
import argparse
import json
import os
import sys
import time

import eth_utils
import requests
from eth_account import Account

DEFAULT_FIXTURE_REL = "state_tests/berlin/eip2930_access_list/test_transaction_intrinsic_gas_cost.json"


def load_fixture_params(fixture_dir, secret, to, sender):
    """Cross-check the CLI params against the EEST fixture (proves reuse)."""
    path = os.path.join(fixture_dir, DEFAULT_FIXTURE_REL)
    if not os.path.exists(path):
        return secret, to, sender  # fixtures absent (local run) -> use defaults
    with open(path) as fh:
        data = json.load(fh)
    for key, test in data.items():
        tx = test.get("transaction", {})
        if not tx:
            continue
        fx_secret = tx.get("secretKey")
        fx_to = tx.get("to")
        fx_sender = tx.get("sender")
        if not fx_secret or not fx_to:
            continue
        if secret and fx_secret.lower() != secret.lower():
            continue
        print(f"[eth-executor] fixture reuse: {key[:60]}...")
        return fx_secret, fx_to, fx_sender
    return secret, to, sender


def rpc_call(url, method, params, timeout=15):
    resp = requests.post(url, json={"jsonrpc": "2.0", "id": 1, "method": method, "params": params},
                         timeout=timeout)
    resp.raise_for_status()
    body = resp.json()
    if "error" in body:
        raise RuntimeError(f"{method} error: {body['error']}")
    return body.get("result")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpc", default="http://127.0.0.1:8545")
    ap.add_argument("--fixture-dir", default="fixtures")
    ap.add_argument("--secret", default="0xa4e7ea6dc542e6de38bf1229c3ea744aa6b9f501386aa167ad42f49e3787066f")
    ap.add_argument("--to", default="0xc3ea744aa6b9f501386aa167ad42f49e3787066f")
    ap.add_argument("--sender", default="0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0")
    args = ap.parse_args()

    secret, to, sender = load_fixture_params(args.fixture_dir, args.secret, args.to, args.sender)

    derived = Account.from_key(secret).address
    assert derived.lower() == sender.lower(), f"sender mismatch: derived={derived} cfg={sender}"

    chain_id = int(rpc_call(args.rpc, "eth_chainId", []), 16)
    nonce = int(rpc_call(args.rpc, "eth_getTransactionCount", [sender, "latest"]), 16)
    balance = int(rpc_call(args.rpc, "eth_getBalance", [sender, "latest"]), 16)
    print(f"[eth-executor] chainId={chain_id} senderNonce={nonce} senderBalanceWei={balance}")
    if balance == 0:
        raise RuntimeError("sender balance is 0 — genesis alloc was not applied")

    # Simple legacy value transfer: 1 wei to the fixture recipient.
    tx = {
        "chainId": chain_id,
        "nonce": nonce,
        "gasPrice": 1,
        "gas": 21000,
        "to": eth_utils.to_checksum_address(to),
        "value": 1,
        "data": b"",
    }
    signed = Account.sign_transaction(tx, secret)
    raw_tx = signed.raw_transaction.hex()
    print(f"[eth-executor] submitting raw tx {raw_tx[:66]}...")

    tx_hash = rpc_call(args.rpc, "eth_sendRawTransaction", [raw_tx])

    receipt = None
    for _ in range(90):
        time.sleep(1)
        receipt = rpc_call(args.rpc, "eth_getTransactionReceipt", [tx_hash])
        if receipt:
            break
    if not receipt:
        raise RuntimeError("receipt not found after 90s — block was not produced/sealed")

    status = receipt.get("status")
    print(f"[eth-executor] receipt status={status} block={receipt.get('blockNumber')} "
          f"gasUsed={receipt.get('gasUsed')}")
    assert status == "0x1", f"transaction failed: {receipt}"

    # Sender must have been debited (value + intrinsic gas).
    bal_after = int(rpc_call(args.rpc, "eth_getBalance", [sender, "latest"]), 16)
    assert bal_after == balance - 21000 - 1, (
        f"sender balance not debited correctly: before={balance} after={bal_after}")
    print(f"[eth-executor] sender debited correctly: {balance} -> {bal_after}")

    # Recipient must have been credited with the value.
    recip_after = int(rpc_call(args.rpc, "eth_getBalance", [to, "latest"]), 16)
    print(f"[eth-executor] recipient balance wei: {recip_after}")
    # KNOWN ISSUE: on an L2 chain (feature_l2_ethereum_compat + MPT state root) a
    # newly-created recipient account is not yet persisted through the commit path,
    # so this assertion currently fails. Tracked separately from this test harness.
    assert recip_after >= 1, (
        f"recipient was not credited (balance={recip_after}); see known L2-commit issue")

    print("[eth-executor] PASS: simple Ethereum value-transfer executed via RPC on "
          "executor_version=2")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[eth-executor] FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
