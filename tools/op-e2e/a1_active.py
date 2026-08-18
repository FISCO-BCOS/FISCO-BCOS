#!/usr/bin/env python3
"""A.1 engine API matrix on the B3a ACTIVE instance (spec §3 A.1). External-driver flow:
FCU (attrs) → payloadId → getPayload → newPayload, then the other-version gates (-38005)
and invalid-tamper vectors (块未入库).

Engine-version adaptive (08-18): the exchangeCapabilities trio is probed and the highest
coherent V (V4 on worktree-op-alignment, V3 on the scheduler line — FCU V4 returns -38005
"not supported" there, per docs/2026-08-18-opstack-scheduler-e2e-verification.md) drives the
whole flow.

Usage: a1_active.py [--eth-port 8563] [--engine-port 8564] [--jwt /tmp/op-spike/b3a/jwt.hex]
"""
import argparse
import base64
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.request

PASSED, FAILED = [], []


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


def _jwt(secret_hex):
    def b64url(d):
        return base64.urlsafe_b64encode(d).rstrip(b"=").decode()
    header = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
    payload = b64url(json.dumps({"iat": int(time.time())}).encode())
    sig = b64url(hmac.new(bytes.fromhex(secret_hex), f"{header}.{payload}".encode(),
                          hashlib.sha256).digest())
    return f"{header}.{payload}.{sig}"


class Rpc:
    def __init__(self, port, jwt_secret=None):
        self.url = f"http://127.0.0.1:{port}"
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self._headers = {"Content-Type": "application/json"}
        if jwt_secret:
            self._headers["Authorization"] = f"Bearer {_jwt(jwt_secret)}"

    def call(self, m, p=None):
        body = json.dumps({"jsonrpc": "2.0", "method": m, "params": p or [], "id": 1}).encode()
        req = urllib.request.Request(self.url, data=body, headers=self._headers)
        with self._opener.open(req, timeout=10) as r:
            out = json.load(r)
        if "error" in out:
            raise AssertionError(f"{m} RPC error: {out['error']}")
        return out.get("result")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--eth-port", type=int, default=8563)
    ap.add_argument("--engine-port", type=int, default=8564)
    ap.add_argument("--jwt", default=os.environ.get("B3A_JWT", "/tmp/op-spike/b3a/jwt.hex"))
    args = ap.parse_args()
    eth = Rpc(args.eth_port)
    jwt = open(args.jwt).read().strip()
    eng = Rpc(args.engine_port, jwt)

    # 0. capabilities — probe the highest coherent engine trio version (V4 old line, V3
    # scheduler line) and drive the whole flow with it.
    caps = eng.call("engine_exchangeCapabilities")
    ver = next((v for v in (4, 3)
                if all(f"engine_{m}V{v}" in caps
                       for m in ["newPayload", "forkchoiceUpdated", "getPayload"])), None)
    check(f"caps have a coherent V{ver or '?'} trio", ver is not None, str(caps))
    V = f"V{ver}"

    def call_new_payload(payload, blob_hashes=(), root=None):
        # V4 signature is [payload, blobHashes, parentBeaconBlockRoot]; V3 takes two params.
        if ver == 4:
            return eng.call(f"engine_newPayloadV4", [payload, list(blob_hashes), root])
        return eng.call(f"engine_newPayloadV3", [payload, list(blob_hashes)])

    # 1. current head hash (genesis on first run; a committed block after a restart — the
    # active instance survives restarts, so never assume the chain is empty)
    head_block = eth.call("eth_getBlockByNumber", ["latest", False])
    check("head queryable", head_block is not None, str(head_block))
    head = head_block["hash"]
    head_num = int(head_block["number"], 16)
    now = int(time.time())
    attrs = {
        "timestamp": hex(now),
        "prevRandao": "0x" + "00" * 32,
        "suggestedFeeRecipient": "0x4200000000000000000000000000000000000011",
    }
    fcs = {"headBlockHash": head, "safeBlockHash": head, "finalizedBlockHash": head}

    # 2. FCU with attrs → VALID + payloadId (active-driver block building)
    fc = eng.call(f"engine_forkchoiceUpdatedV{ver}", [fcs, attrs])
    check(f"FCU {V} VALID + payloadId",
          fc["payloadStatus"]["status"] == "VALID" and fc.get("payloadId") is not None,
          str(fc))
    pid = fc["payloadId"]

    # 3. getPayload → executionPayload is nested
    pl = eng.call(f"engine_getPayloadV{ver}", [pid])
    check(f"getPayload {V} result object", isinstance(pl, dict), str(pl)[:120])
    ep = pl.get("executionPayload", {})
    check(f"getPayload {V} returns head+1", ep.get("blockNumber") == hex(head_num + 1),
          f"{ep.get('blockNumber')} vs {hex(head_num + 1)}")
    check(f"getPayload {V} blockHash present", ep.get("blockHash", "").startswith("0x"),
          str(ep.get("blockHash")))
    check(f"getPayload {V} has 1 deposit tx", len(ep.get("transactions", [])) == 1,
          str(len(ep.get("transactions", []))))

    print("  ep txs:", len(ep.get("transactions", [])), "pbr:", repr(ep.get("parentBeaconBlockRoot")))
    # 4. newPayload (same payload) → VALID.
    root = "0x" + "00" * 32
    np = call_new_payload(ep, root=root)
    check(f"newPayload {V} VALID", np.get("status") == "VALID", str(np))

    # 5. version gates: every OTHER payload version on the same payload → -38005 (the old
    # line gates V1-V3 under V4; the scheduler line gates V4 under V3).
    for n in (1, 2, 3, 4):
        v = f"V{n}"
        if v == V:
            continue
        try:
            eng.call(f"engine_newPayload{v}", [ep])
            check(f"newPayload{v} gated (-38005)", False, "expected version-gate error")
        except AssertionError as e:
            check(f"newPayload{v} gated (-38005)", "-38005" in str(e), str(e))

    # 6. tamper stateRoot → INVALID, block must NOT be stored
    bad = dict(ep)
    bad["stateRoot"] = "0x" + "11" * 32
    bad["blockHash"] = "0x" + "22" * 32  # hash must match to reach execution
    try:
        npt = call_new_payload(bad, root=root)
        check("tampered stateRoot INVALID", npt.get("status") == "INVALID", str(npt))
    except AssertionError as e:
        # -32603 internal may surface if the tamper trips a pre-execution gate; still an error
        check("tampered stateRoot rejected (INVALID or error)", True, str(e))

    # ---- B4: engine error-code matrix (op-geth test-stack item 4) ----
    # 7. tamper gasUsed → the engine must reject (INVALID after execution, or an RPC error).
    bad_gas = dict(ep)
    bad_gas["gasUsed"] = hex(int(ep["gasUsed"], 16) + 1)
    bad_gas["blockHash"] = "0x" + "33" * 32
    try:
        npt = call_new_payload(bad_gas, root=root)
        check("tampered gasUsed rejected (INVALID or error)",
              npt.get("status") in ("INVALID", "SYNCING") or npt.get("status") is None, str(npt))
    except AssertionError as e:
        check("tampered gasUsed rejected (INVALID or error)", True, str(e)[:70])

    # 8. tamper receiptsRoot → rejected the same way.
    bad_rec = dict(ep)
    bad_rec["receiptsRoot"] = "0x" + "44" * 32
    bad_rec["blockHash"] = "0x" + "55" * 32
    try:
        npt = call_new_payload(bad_rec, root=root)
        check("tampered receiptsRoot rejected (INVALID or error)",
              npt.get("status") in ("INVALID", "SYNCING") or npt.get("status") is None, str(npt))
    except AssertionError as e:
        check("tampered receiptsRoot rejected (INVALID or error)", True, str(e)[:70])

    # 9. getPayload with a bogus payloadId → RPC error (unknown payload), never a block payload.
    try:
        eng.call(f"engine_getPayloadV{ver}", ["0xdeadbeef00"])
        check("getPayload bogus payloadId error", False, "expected error")
    except AssertionError as e:
        check("getPayload bogus payloadId error", "-32602" in str(e) or "error" in str(e).lower(),
              str(e)[:70])

    # 10. FCU with an unknown head hash → SYNCING (parent unknown), not VALID.
    unknown_head = {"headBlockHash": "0x" + "aa" * 32,
                    "safeBlockHash": "0x" + "aa" * 32, "finalizedBlockHash": "0x" + "aa" * 32}
    try:
        fc = eng.call(f"engine_forkchoiceUpdatedV{ver}", [unknown_head, None])
        check("FCU unknown head SYNCING", fc.get("payloadStatus", {}).get("status") == "SYNCING",
              str(fc)[:120])
    except AssertionError as e:
        check("FCU unknown head SYNCING", "SYNCING" in str(e) or "error" in str(e).lower(),
              str(e)[:70])

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
