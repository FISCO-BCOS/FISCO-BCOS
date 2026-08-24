#!/usr/bin/env python3
"""A.1 engine API matrix on the B3a ACTIVE instance (spec §3 A.1). Tier-2 attribute-driven
building: FCU with attrs builds a payload (VALID + payloadId) and getPayload serves it;
the attrs surface mirrors op-node's rollup shape (gasLimit/eip1559Params/withdrawals/
parentBeaconBlockRoot/minBaseFee) and the engine validates it (op-geth
checkOptimismPayloadAttributes) instead of silently normalizing deviations.
Payload-level flows (newPayload execution, -38005 version gates, tamper vectors) live in
the C++ harness; this script asserts the FCU/label surface: attrs-less FCU VALID without
payloadId, attrs FCU VALID + payloadId (Tier-2 build), getPayload serving the built
payload (Jovian minBaseFee tail audit), safe/finalized routing to the tracked head
(strict null pre-FCU), pending aliasing latest, and unknown-head SYNCING.

Engine-version adaptive (08-18): the exchangeCapabilities trio is probed and the highest
coherent V (V4 on worktree-op-alignment, V3 on the scheduler line — FCU V4 returns -38005
"not supported" there, per docs/2026-08-18-opstack-scheduler-e2e-verification.md) drives
the whole flow.

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


def fail_out():
    """Print the summary and exit 1 — used when a failed check leaves nothing to assert on."""
    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
    sys.exit(1)


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
    if ver is None:
        fail_out()  # every later engine call would render engine_*VNone
    V = f"V{ver}"

    # 1. head (genesis on fresh B3a; at HEAD the pull model has no in-process builder,
    # so the chain never advances — payload-side flows live in the C++ harness until Tier-2)
    head_block = eth.call("eth_getBlockByNumber", ["latest", False])
    check("head queryable", head_block is not None, str(head_block))
    if head_block is None:
        fail_out()  # head_block["hash"] below would crash
    head = head_block["hash"]

    # 2. pre-FCU label state: strict op-geth semantics — null when nothing tracked.
    # Tolerant to a previously-FCU'd persistent node (tracking survives until restart).
    try:
        pre_safe = eth.call("eth_getBlockByNumber", ["safe", False])
    except AssertionError:
        pre_safe = None  # an error response is also acceptable pre-FCU (state endpoints)
    check("pre-FCU safe is null (fresh) or the FCU'd head (persistent)",
          pre_safe is None or pre_safe["number"] == head_block["number"],
          str(pre_safe and pre_safe["number"]))

    # 3. FCU attrs-less -> VALID, no payloadId (the pull-model head/safe/finalized update)
    fcs = {"headBlockHash": head, "safeBlockHash": head, "finalizedBlockHash": head}
    fc = eng.call(f"engine_forkchoiceUpdatedV{ver}", [fcs, None])
    check(f"FCU {V} attrs-less VALID, no payloadId",
          fc["payloadStatus"]["status"] == "VALID" and fc.get("payloadId") is None, str(fc))

    # 4. FCU with attrs -> Tier-2 attribute-driven building: VALID + payloadId. The attrs mirror
    # op-node's rollup shape (op-service/eth/types.go PayloadAttributes): gasLimit from the head
    # (SystemConfig-derived), Holocene eip1559Params (denominator=8, elasticity=2), empty
    # withdrawals, parentBeaconBlockRoot, and (Jovian chains) minBaseFee. The engine validates
    # this surface (op-geth checkOptimismPayloadAttributes) and refuses to build on deviation.
    now = int(time.time())
    head_gas = int(head_block["gasLimit"], 16)
    genesis_extra = eth.call("eth_getBlockByNumber", ["0x0", False])["extraData"]
    jovian = len(bytes.fromhex(genesis_extra[2:])) == 17  # Jovian 17B extraData shape
    attrs = {"timestamp": hex(now), "prevRandao": "0x" + "00" * 32,
             "suggestedFeeRecipient": "0x4200000000000000000000000000000000000011",
             "gasLimit": hex(head_gas),
             "eip1559Params": "0x0000000800000002",
             "withdrawals": [],
             "parentBeaconBlockRoot": "0x" + "00" * 32}
    if jovian:
        attrs["minBaseFee"] = "0x3b9aca00"
    fc2 = eng.call(f"engine_forkchoiceUpdatedV{ver}", [fcs, attrs])
    check("FCU attrs builds (VALID + payloadId, Tier-2)",
          fc2["payloadStatus"]["status"] == "VALID" and fc2.get("payloadId") is not None,
          str(fc2)[:120])

    # 5. getPayload serves the built payload (Tier-2). Jovian chains: the extraData tail
    # [9,17) must echo the requested minBaseFee (audit BL-3).
    pl = eng.call(f"engine_getPayloadV{ver}", [fc2["payloadId"]])
    pl_head = pl["executionPayload"]
    check("getPayload serves the built payload (Tier-2)",
          int(pl_head["blockNumber"], 16) == int(head_block["number"], 16) + 1,
          str(pl)[:120])
    if jovian:
        extra = bytes.fromhex(pl_head["extraData"][2:])
        check("Jovian extraData minBaseFee tail (BL-3)",
              len(extra) == 17 and int.from_bytes(extra[9:17], "big") == 1000000000,
              str(extra))

    # 6. post-FCU labels: routed to the tracked (genesis) head — hash equality with the
    # FCU hashes proves the tracked path (not the latest alias) served them
    for tag in ("safe", "finalized"):
        b = eth.call("eth_getBlockByNumber", [tag, False])
        check(f"{tag} routed to FCU'd head (hash match)",
              b is not None and b["hash"] == head and b["number"] == head_block["number"],
              str(b and b.get("hash")))

    # 7. pending guard: stays aliased to latest
    p = eth.call("eth_getBlockByNumber", ["pending", False])
    check("pending aliases latest", p is not None and p["hash"] == head, str(p and p["hash"]))

    # 8. version gate at the capability level: no newPayload major version ABOVE the
    # coherent trio is advertised (EngineServiceImpl supportedOpCapabilities deliberately
    # hides V4 until its RPC endpoints exist — op-node must not negotiate to a -38005 stub).
    # Lower versions (V1/V2) are advertised alongside V3, matching upstream geth semantics
    # (every supported version of a method is listed). Payload-level -38005 gates need a
    # payload source — Tier-2.
    gated = [f"engine_newPayloadV{n}" for n in (1, 2, 3, 4)
             if n > ver and f"engine_newPayloadV{n}" in caps]
    check(f"no newPayload major version above the coherent trio advertised (V{ver})",
          not gated, str(caps))

    # 9. unknown-head FCU -> SYNCING (parent unknown), not VALID
    unknown_head = {"headBlockHash": "0x" + "aa" * 32,
                    "safeBlockHash": "0x" + "aa" * 32, "finalizedBlockHash": "0x" + "aa" * 32}
    try:
        fc3 = eng.call(f"engine_forkchoiceUpdatedV{ver}", [unknown_head, None])
        check("FCU unknown head SYNCING",
              fc3.get("payloadStatus", {}).get("status") == "SYNCING", str(fc3)[:120])
    except AssertionError as e:
        check("FCU unknown head SYNCING", "SYNCING" in str(e), str(e)[:70])

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
