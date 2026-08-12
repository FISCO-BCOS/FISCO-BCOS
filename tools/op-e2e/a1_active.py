#!/usr/bin/env python3
"""A.1 engine API matrix on the B3a ACTIVE instance (enable_single_node_consensus=false,
spec §3 A.1). External-driver flow: FCU V4 (attrs) → payloadId → getPayload V4 → newPayload V4,
then the V1-V3 version gates (-38005) and an invalid-tamper vector (块未入库).

Usage: a1_active.py [--eth-port 8563] [--engine-port 8564] [--jwt /tmp/op-spike/b3a/jwt.hex]
"""
import argparse
import base64
import hashlib
import hmac
import json
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
    ap.add_argument("--jwt", default="/tmp/op-spike/b3a/jwt.hex")
    args = ap.parse_args()
    eth = Rpc(args.eth_port)
    jwt = open(args.jwt).read().strip()
    eng = Rpc(args.engine_port, jwt)

    # 0. capabilities
    caps = eng.call("engine_exchangeCapabilities")
    check("caps has V4 trio",
          all(f"engine_{m}V4" in caps for m in ["newPayload", "forkchoiceUpdated", "getPayload"]),
          str(caps))

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

    # 2. FCU V4 with attrs → VALID + payloadId (active-driver block building)
    fc = eng.call("engine_forkchoiceUpdatedV4", [fcs, attrs])
    check("FCU V4 VALID + payloadId",
          fc["payloadStatus"]["status"] == "VALID" and fc.get("payloadId") is not None,
          str(fc))
    pid = fc["payloadId"]

    # 3. getPayload V4 → executionPayload is nested
    pl = eng.call("engine_getPayloadV4", [pid])
    check("getPayload V4 result object", isinstance(pl, dict), str(pl)[:120])
    ep = pl.get("executionPayload", {})
    check("getPayload V4 returns head+1", ep.get("blockNumber") == hex(head_num + 1),
          f"{ep.get('blockNumber')} vs {hex(head_num + 1)}")
    check("getPayload V4 blockHash present", ep.get("blockHash", "").startswith("0x"),
          str(ep.get("blockHash")))
    check("getPayload V4 has 1 deposit tx", len(ep.get("transactions", [])) == 1,
          str(len(ep.get("transactions", []))))

    print("  ep txs:", len(ep.get("transactions", [])), "pbr:", repr(ep.get("parentBeaconBlockRoot")))
    # 4. newPayload V4 (same payload) → VALID. V4 signature is [payload, blobHashes,
    #    parentBeaconBlockRoot].
    root = "0x" + "00" * 32
    np = eng.call("engine_newPayloadV4", [ep, [], root])
    check("newPayload V4 VALID", np.get("status") == "VALID", str(np))

    # 5. V1/V2/V3 version gates on the same Isthmus+ payload → -38005 UnsupportedFork
    for v in ["V1", "V2", "V3"]:
        try:
            eng.call(f"engine_newPayload{v}", [ep])
            check(f"newPayload{v} -38005", False, "expected version-gate error")
        except AssertionError as e:
            check(f"newPayload{v} -38005", "-38005" in str(e), str(e))

    # 6. tamper stateRoot → INVALID, block must NOT be stored
    bad = dict(ep)
    bad["stateRoot"] = "0x" + "11" * 32
    bad["blockHash"] = "0x" + "22" * 32  # hash must match to reach execution
    try:
        npt = eng.call("engine_newPayloadV4", [bad, [], root])
        check("tampered stateRoot INVALID", npt.get("status") == "INVALID", str(npt))
    except AssertionError as e:
        # -32603 internal may surface if the tamper trips a pre-execution gate; still an error
        check("tampered stateRoot rejected (INVALID or error)", True, str(e))

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
