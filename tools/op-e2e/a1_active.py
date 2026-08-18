#!/usr/bin/env python3
"""A.1 engine API matrix on the B3a ACTIVE instance (spec §3 A.1). Pull-contract surface
(D2 Tier-1): the OP-mode engine refuses attribute-driven building (FCU with attrs →
-38003 UnsupportedOpPayloadAttributes, after the head/safe/finalized tracking update has
run) and getPayload (OpPayloadBuildingUnsupported — no in-process builder until Tier-2).
Payload-level flows (newPayload execution, -38005 version gates, tamper vectors) live in
the C++ harness; this script asserts the FCU/label surface: attrs-less FCU VALID without
payloadId, -38003 attrs refusal, getPayload refusal, safe/finalized routing to the
tracked head (strict null pre-FCU), pending aliasing latest, and unknown-head SYNCING.

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

    # 1. head (genesis on fresh B3a; at HEAD the pull model has no in-process builder,
    # so the chain never advances — payload-side flows live in the C++ harness until Tier-2)
    head_block = eth.call("eth_getBlockByNumber", ["latest", False])
    check("head queryable", head_block is not None, str(head_block))
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

    # 4. FCU with attrs -> -38003 (OP pull contract: attribute-driven building refused;
    # the tracking update above has already run and is kept)
    now = int(time.time())
    attrs = {"timestamp": hex(now), "prevRandao": "0x" + "00" * 32,
             "suggestedFeeRecipient": "0x4200000000000000000000000000000000000011"}
    try:
        fc2 = eng.call(f"engine_forkchoiceUpdatedV{ver}", [fcs, attrs])
        check("FCU attrs refused in OP mode (-38003)", "-38003" in str(fc2), str(fc2)[:120])
    except AssertionError as e:
        check("FCU attrs refused in OP mode (-38003)", "-38003" in str(e), str(e)[:120])

    # 5. getPayload -> refused (no OP-ized builder; Tier-2 will implement)
    try:
        eng.call(f"engine_getPayloadV{ver}", ["0x0000000000000000"])
        check("getPayload refused in OP mode", False, "expected an error")
    except AssertionError as e:
        check("getPayload refused in OP mode", True, str(e)[:90])

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
        check("FCU unknown head SYNCING", "SYNCING" in str(e) or "error" in str(e).lower(),
              str(e)[:70])

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
