#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""
mock_cl_el_acceptance.py — Phase 7 Layer-3 acceptance: drive the EL-mode (Sepolia)
fisco-bcos node through the Engine API like a consensus client would.

Unlike mock_consensus_client.py (which targets the L2/Karst lane with deposits and
getPayloadV5), this script exercises the L1 EL dialect on a node whose chain head is
the real Sepolia genesis:

  1. capabilities + client version exchange shapes
  2. FCU to the known head (genesis)               -> VALID
  3. FCU to an unknown head                        -> SYNCING (+ backfill request log)
  4. closed loop: FCU(+attributes) -> getPayload -> newPayload -> FCU  (London-era
     synthetic blocks — the Sepolia fork schedule gates Paris at block 1735371, so a
     chain grown from genesis can only ever produce London-era blocks)
  5. idempotent replay: newPayload of an already-committed block -> VALID
  6. shallow rollback: FCU back to an earlier committed block -> VALID, then forward
  7. getPayloadBodiesByHash/ByRange + getBlobsV1 response shapes

Usage:
  python3 tests/mock_cl_el_acceptance.py [--engine http://127.0.0.1:18551]
      [--web3 http://127.0.0.1:18545] [--jwt conf/engine/jwt.hex]

Exit code: 0 = all checks passed, 1 = any failure.
"""

import argparse
import base64
import hashlib
import hmac
import json
import sys
import time

import requests

PASS = 0
FAIL = 0
GREEN, RED, YELLOW, NC = "\033[0;32m", "\033[0;31m", "\033[1;33m", "\033[0m"

ZERO_HASH = "0x" + "00" * 32


def _log_test(name):
    print(f"{GREEN}[TEST]{NC} {name}")


def _log_pass(msg=""):
    global PASS
    PASS += 1
    print(f"  {GREEN}PASS{NC} {msg}")


def _log_fail(msg):
    global FAIL
    FAIL += 1
    print(f"  {RED}FAIL: {msg}{NC}")


def _log_info(msg):
    print(f"  {YELLOW}info{NC} {msg}")


class Engine:
    def __init__(self, engine_url, web3_url, jwt_secret):
        self.engine_url = engine_url
        self.web3_url = web3_url
        self.secret = jwt_secret
        self.req_id = 0

    def _jwt(self):
        def b64u(b):
            return base64.urlsafe_b64encode(b).rstrip(b"=")
        hdr = b64u(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
        pay = b64u(json.dumps({"iat": int(time.time())}).encode())
        sig = b64u(hmac.new(self.secret, hdr + b"." + pay, hashlib.sha256).digest())
        return (hdr + b"." + pay + b"." + sig).decode()

    def engine(self, method, params):
        self.req_id += 1
        resp = requests.post(
            self.engine_url,
            json={"jsonrpc": "2.0", "id": self.req_id, "method": method, "params": params},
            headers={"Authorization": "Bearer " + self._jwt()},
            timeout=60,
        )
        resp.raise_for_status()
        return resp.json()

    def web3(self, method, params):
        self.req_id += 1
        resp = requests.post(
            self.web3_url,
            json={"jsonrpc": "2.0", "id": self.req_id, "method": method, "params": params},
            timeout=30,
        )
        resp.raise_for_status()
        return resp.json()

    def engine_result(self, method, params):
        data = self.engine(method, params)
        if "error" in data:
            raise RuntimeError(f"RPC error [{method}]: {data['error']}")
        return data["result"]

    def web3_result(self, method, params):
        data = self.web3(method, params)
        if "error" in data:
            raise RuntimeError(f"RPC error [{method}]: {data['error']}")
        return data["result"]


def expect_status(eng, label, method, params, want_status):
    """Call an engine method answering a PayloadStatus/ForkchoiceUpdatedResult and
    assert the .status (or .payloadStatus.status) field."""
    _log_test(f"{label}: {method} -> {want_status}")
    data = eng.engine(method, params)
    if "error" in data:
        _log_fail(f"RPC error: {data['error']}")
        return None
    result = data["result"]
    status_obj = result.get("payloadStatus", result)
    got = status_obj.get("status")
    if got != want_status:
        _log_fail(f"status {got} != {want_status} (full: {json.dumps(result)[:300]})")
        return None
    _log_pass(f"status={got}")
    return result


def block_to_payload_v1(block):
    """Map an eth_getBlockByNumber JSON block onto ExecutionPayloadV1 (Paris shape)."""
    return {
        "parentHash": block["hash"] and block["parentHash"],
        "feeRecipient": block.get("miner") or block.get("feeRecipient"),
        "stateRoot": block["stateRoot"],
        "receiptsRoot": block["receiptsRoot"],
        "logsBloom": block["logsBloom"],
        "prevRandao": block.get("mixHash", ZERO_HASH),
        "blockNumber": block["number"],
        "gasLimit": block["gasLimit"],
        "gasUsed": block["gasUsed"],
        "timestamp": block["timestamp"],
        "extraData": block["extraData"],
        "baseFeePerGas": block.get("baseFeePerGas", "0x0"),
        "blockHash": block["hash"],
        "transactions": [],
    }


def produce_block(eng, parent, timestamp, tag):
    """One full closed loop on top of `parent` (an eth block JSON). Returns the
    committed payload dict, or None on failure."""
    _log_test(f"closed loop {tag}: build block {int(parent['number'], 16) + 1} "
              f"(ts={timestamp})")
    attrs = {
        "timestamp": hex(timestamp),
        "prevRandao": "0x" + "00" * 31 + "01",
        "suggestedFeeRecipient": "0x0000000000000000000000000000000000000001",
        # The node requires the (possibly empty) withdrawals list on V2 attributes even
        # pre-Shanghai; FCU V1 without it also works but V2 is the Shanghai-ready shape.
        "withdrawals": [],
    }
    state = {
        "headBlockHash": parent["hash"],
        "safeBlockHash": parent["hash"],
        "finalizedBlockHash": parent["hash"],
    }
    fcu = eng.engine("engine_forkchoiceUpdatedV2", [state, attrs])
    if "error" in fcu:
        _log_fail(f"FCU(+attrs) error: {fcu['error']}")
        return None
    result = fcu["result"]
    if result["payloadStatus"]["status"] != "VALID":
        _log_fail(f"FCU(+attrs) status {result['payloadStatus']} != VALID")
        return None
    payload_id = result.get("payloadId")
    if not payload_id:
        _log_fail(f"FCU(+attrs) returned no payloadId: {result}")
        return None
    _log_info(f"payloadId={payload_id}")

    got = eng.engine("engine_getPayloadV2", [payload_id])
    if "error" in got:
        _log_fail(f"getPayloadV2 error: {got['error']}")
        return None
    payload = got["result"]["executionPayload"]
    want_number = int(parent["number"], 16) + 1
    if int(payload["blockNumber"], 16) != want_number:
        _log_fail(f"payload number {payload['blockNumber']} != {hex(want_number)}")
        return None
    if payload["parentHash"].lower() != parent["hash"].lower():
        _log_fail(f"payload parent {payload['parentHash']} != {parent['hash']}")
        return None
    _log_info(f"built block {want_number} hash={payload['blockHash'][:18]}… "
              f"txs={len(payload['transactions'])} gasUsed={payload['gasUsed']}")

    np = eng.engine("engine_newPayloadV1", [payload])
    if "error" in np:
        _log_fail(f"newPayloadV1 error: {np['error']}")
        return None
    if np["result"]["status"] != "VALID":
        _log_fail(f"newPayloadV1 status {np['result']} != VALID")
        return None

    state2 = {
        "headBlockHash": payload["blockHash"],
        "safeBlockHash": payload["blockHash"],
        "finalizedBlockHash": parent["hash"],
    }
    fcu2 = eng.engine("engine_forkchoiceUpdatedV2", [state2, None])
    if "error" in fcu2 or fcu2["result"]["payloadStatus"]["status"] != "VALID":
        _log_fail(f"FCU(head=new) -> {fcu2}")
        return None
    chain_number = int(eng.web3_result("eth_blockNumber", []), 16)
    if chain_number != want_number:
        _log_fail(f"eth_blockNumber {chain_number} != {want_number} after FCU")
        return None
    _log_pass(f"block {want_number} committed and is the chain head")
    return payload


def build_only(eng, parent, timestamp):
    """FCU(+attributes) -> getPayload, WITHOUT newPayload: the payload stays
    uncommitted, known only to this process's payload cache."""
    attrs = {
        "timestamp": hex(timestamp),
        "prevRandao": "0x" + "00" * 31 + "01",
        "suggestedFeeRecipient": "0x0000000000000000000000000000000000000001",
        "withdrawals": [],
    }
    state = {
        "headBlockHash": parent["hash"],
        "safeBlockHash": parent["hash"],
        "finalizedBlockHash": parent["hash"],
    }
    fcu = eng.engine("engine_forkchoiceUpdatedV2", [state, attrs])
    if "error" in fcu:
        raise RuntimeError(f"FCU(+attrs) error: {fcu['error']}")
    payload_id = fcu["result"].get("payloadId")
    if not payload_id:
        raise RuntimeError(f"no payloadId: {fcu}")
    got = eng.engine_result("engine_getPayloadV2", [payload_id])
    return got["executionPayload"]


def save_payload_mode(eng, path):
    """Phase A of the external-lane demonstration: build (but do not commit) one
    block on the current head and save it. Restart the node afterwards so its
    payload cache forgets this block, then run --external-replay."""
    head_number = int(eng.web3_result("eth_blockNumber", []), 16)
    head_block = eng.web3_result("eth_getBlockByNumber", [hex(head_number), False])
    payload = build_only(eng, head_block, int(head_block["timestamp"], 16) + 12)
    with open(path, "w") as fh:
        json.dump(payload, fh, indent=1)
    _log_test(f"payload for block {payload['blockNumber']} saved to {path}")
    _log_pass(f"hash={payload['blockHash']} parent={payload['parentHash'][:18]}… "
              f"— now RESTART the node and run --external-replay {path}")
    return 0


def external_replay_mode(eng, path):
    """Phase B: push the saved payload to the restarted node. The payload cache is
    cold, so the block is NOT built-here: it routes to the external lane and is
    executed by the shared Ethereum block verifier (which also journals the commit,
    enabling the rollback checks afterwards)."""
    payload = json.load(open(path))
    genesis = eng.web3_result("eth_getBlockByNumber", ["0x0", False])
    parent_number = int(payload["blockNumber"], 16) - 1
    parent = eng.web3_result("eth_getBlockByNumber", [hex(parent_number), False])
    if parent["hash"].lower() != payload["parentHash"].lower():
        _log_fail(f"saved payload parent {payload['parentHash']} != local block "
                  f"{parent_number} ({parent['hash']}) — stale save file?")
        return 1

    expect_status(eng, "external newPayload (verifier-executed)",
                  "engine_newPayloadV1", [payload], "VALID")
    expect_status(eng, "FCU head -> external block", "engine_forkchoiceUpdatedV2",
                  [{"headBlockHash": payload["blockHash"],
                    "safeBlockHash": payload["blockHash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "VALID")
    n = int(eng.web3_result("eth_blockNumber", []), 16)
    if n != int(payload["blockNumber"], 16):
        _log_fail(f"eth_blockNumber {n} != {payload['blockNumber']}")
    else:
        _log_pass(f"chain head is now block {n}")

    # The verifier journaled this commit, so rewinding over it must succeed.
    expect_status(eng, "rollback head -> parent (journaled commit)",
                  "engine_forkchoiceUpdatedV2",
                  [{"headBlockHash": parent["hash"],
                    "safeBlockHash": parent["hash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "VALID")
    n = int(eng.web3_result("eth_blockNumber", []), 16)
    if n != parent_number:
        _log_fail(f"after rollback eth_blockNumber={n}, want {parent_number}")
    else:
        _log_pass(f"head rewound to {parent_number}")
    # After the rewind the block is gone from the committed chain, so a bare FCU to its
    # hash answers SYNCING — the CL's documented recovery is to re-send newPayload (the
    # external lane re-executes it) and only then re-point the forkchoice.
    expect_status(eng, "bare FCU to rewound block -> SYNCING (re-request)",
                  "engine_forkchoiceUpdatedV2",
                  [{"headBlockHash": payload["blockHash"],
                    "safeBlockHash": payload["blockHash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "SYNCING")
    expect_status(eng, "re-apply: newPayload re-executes", "engine_newPayloadV1",
                  [payload], "VALID")
    expect_status(eng, "re-apply: FCU head -> external block", "engine_forkchoiceUpdatedV2",
                  [{"headBlockHash": payload["blockHash"],
                    "safeBlockHash": payload["blockHash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "VALID")
    n = int(eng.web3_result("eth_blockNumber", []), 16)
    if n != int(payload["blockNumber"], 16):
        _log_fail(f"after re-apply eth_blockNumber={n}, want {payload['blockNumber']}")
    else:
        _log_pass(f"head restored to {n}")

    print()
    print(f"{'=' * 60}")
    total = PASS + FAIL
    print(f"Pass rate: {100.0 * PASS / total if total else 0:.2f}%  "
          f"(PASS={PASS} FAIL={FAIL} of {total})")
    return 0 if FAIL == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default="http://127.0.0.1:18551")
    ap.add_argument("--web3", default="http://127.0.0.1:18545")
    ap.add_argument("--jwt", default="conf/engine/jwt.hex")
    ap.add_argument("--save-payload", default=None, metavar="FILE",
                    help="build one uncommitted payload on the head, save it, exit")
    ap.add_argument("--external-replay", default=None, metavar="FILE",
                    help="push a saved payload to a node restarted after --save-payload")
    args = ap.parse_args()

    secret = bytes.fromhex(open(args.jwt).read().strip())
    eng = Engine(args.engine, args.web3, secret)

    if args.save_payload:
        return save_payload_mode(eng, args.save_payload)
    if args.external_replay:
        return external_replay_mode(eng, args.external_replay)

    # ---------------------------------------------------------------- 1. shapes
    _log_test("engine_exchangeCapabilities advertises the EL method set")
    caps = eng.engine_result("engine_exchangeCapabilities", [[]])
    required = {
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3",
        "engine_getPayloadV2", "engine_getPayloadV3",
        "engine_newPayloadV2", "engine_newPayloadV3",
        "engine_getPayloadBodiesByHashV1", "engine_getPayloadBodiesByRangeV1",
        "engine_getBlobsV1", "engine_exchangeClientVersionV1",
    }
    missing = required - set(caps)
    if missing:
        _log_fail(f"missing capabilities: {sorted(missing)}")
    else:
        _log_pass(f"{len(caps)} methods")

    _log_test("engine_exchangeClientVersionV1 / getClientVersionV1 shapes")
    cv = eng.engine_result("engine_exchangeClientVersionV1", [
        {"code": "XX", "name": "phase7-mock-cl", "version": "0.0.1", "commit": "00000000"}])
    ok = (isinstance(cv, list) and cv and
          all(k in cv[0] for k in ("code", "name", "version", "commit")))
    if ok:
        _log_pass(f"EL identifies as {cv[0]}")
    else:
        _log_fail(f"bad ClientVersionV1 shape: {cv}")

    # ------------------------------------------------- 2. FCU to the known head
    genesis = eng.web3_result("eth_getBlockByNumber", ["0x0", False])
    _log_info(f"genesis hash={genesis['hash']} ts={int(genesis['timestamp'], 16)}")
    head_number = int(eng.web3_result("eth_blockNumber", []), 16)
    _log_info(f"local chain head before the run: {head_number}")
    head_block = eng.web3_result("eth_getBlockByNumber", [hex(head_number), False])

    expect_status(eng, "known head", "engine_forkchoiceUpdatedV2",
                  [{"headBlockHash": head_block["hash"],
                    "safeBlockHash": genesis["hash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "VALID")

    # ---------------------------------------------- 3. FCU to an unknown head
    unknown = "0x" + "ab" * 32
    expect_status(eng, "unknown head", "engine_forkchoiceUpdatedV3",
                  [{"headBlockHash": unknown,
                    "safeBlockHash": genesis["hash"],
                    "finalizedBlockHash": genesis["hash"]}, None], "SYNCING")
    _log_info("check the node log for a backfill request targeting 0xabab…")

    # ----------------------------------------------------- 4. closed loop x2
    parent = head_block
    ts = int(parent["timestamp"], 16) + 12
    payloads = []
    for i in (1, 2):
        payload = produce_block(eng, parent, ts, f"#{i}")
        if payload is None:
            break
        payloads.append(payload)
        parent = eng.web3_result("eth_getBlockByHash", [payload["blockHash"], False])
        ts += 12

    # ------------------------------------- 5. idempotent replay of a committed
    if payloads:
        replay = dict(payloads[0])
        expect_status(eng, "replay committed block", "engine_newPayloadV1",
                      [replay], "VALID")

    # ------------------------------------------------------- 6. shallow rollback
    # NB: blocks committed through the BUILT-HERE path carry no rollback journal (only
    # the EthereumBlockVerifier commit — sync / external lane — writes one), so an FCU
    # that would rewind over them is refused with SYNCING. That is the safe degradation:
    # the CL falls back to network backfill. The rewind itself is exercised live by the
    # --external-replay mode, whose block commits through the verifier (journaled).
    if len(payloads) == 2:
        older, newer = payloads
        expect_status(eng, "rollback over built-here block refused (no journal)",
                      "engine_forkchoiceUpdatedV2",
                      [{"headBlockHash": older["blockHash"],
                        "safeBlockHash": older["blockHash"],
                        "finalizedBlockHash": genesis["hash"]}, None], "SYNCING")
        n = int(eng.web3_result("eth_blockNumber", []), 16)
        if n != int(newer["blockNumber"], 16):
            _log_fail(f"refused rollback still moved the head: {n}")
        else:
            _log_pass(f"head stayed at {n}")
        expect_status(eng, "re-affirm head -> block N+1", "engine_forkchoiceUpdatedV2",
                      [{"headBlockHash": newer["blockHash"],
                        "safeBlockHash": newer["blockHash"],
                        "finalizedBlockHash": genesis["hash"]}, None], "VALID")

    # ---------------------------------------------------------- 7. body shapes
    _log_test("engine_getPayloadBodiesByHashV1 shape (known + unknown)")
    hashes = [p["blockHash"] for p in payloads] + ["0x" + "cd" * 32]
    bodies = eng.engine_result("engine_getPayloadBodiesByHashV1", [hashes])
    if not isinstance(bodies, list) or len(bodies) != len(hashes):
        _log_fail(f"bodies length {len(bodies) if isinstance(bodies, list) else bodies} "
                  f"!= {len(hashes)}")
    else:
        shape_ok = all((b is None) or ("transactions" in b) for b in bodies)
        known_ok = all(bodies[i] is not None for i in range(len(payloads)))
        unknown_ok = bodies[-1] is None
        if shape_ok and known_ok and unknown_ok:
            _log_pass(f"{len(payloads)} known bodies + null for the unknown hash")
        else:
            _log_fail(f"bad bodies: {json.dumps(bodies)[:300]}")

    if payloads:
        _log_test("engine_getPayloadBodiesByRangeV1 shape")
        start = int(payloads[0]["blockNumber"], 16)
        bodies = eng.engine_result("engine_getPayloadBodiesByRangeV1",
                                   [hex(start), hex(len(payloads))])
        if isinstance(bodies, list) and len(bodies) == len(payloads) and all(
                b is not None and "transactions" in b for b in bodies):
            _log_pass(f"range [{start}, +{len(payloads)}) served")
        else:
            _log_fail(f"bad range bodies: {json.dumps(bodies)[:300]}")

    _log_test("engine_getBlobsV1 shapes (empty + unknown versioned hash)")
    empty = eng.engine_result("engine_getBlobsV1", [[]])
    unknown_vh = ["0x01" + "ff" * 31]
    miss = eng.engine_result("engine_getBlobsV1", [unknown_vh])
    if empty == [] and miss == [None]:
        _log_pass("[] -> [], unknown -> [null]")
    else:
        _log_fail(f"empty={empty} miss={miss}")

    print()
    print(f"{'=' * 60}")
    total = PASS + FAIL
    print(f"Pass rate: {100.0 * PASS / total if total else 0:.2f}%  "
          f"(PASS={PASS} FAIL={FAIL} of {total})")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
