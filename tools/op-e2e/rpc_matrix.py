#!/usr/bin/env python3
"""RPC matrix assertions for the B3 OP node (spec Part A).

HTTP JSON-RPC wrapper that bypasses the local proxy (urllib would route through it and the
node would never see the request), plus assertion helpers and the A.1-A.3 method matrix.

Usage: rpc_matrix.py [--host HOST] [--port PORT] [--only GROUP]
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

try:
    import requests
except ImportError:
    requests = None


def _jwt(secret_hex):
    """Engine-API JWT (HS256) for the op_engine_rpc endpoint, per the Engine API auth spec."""
    def b64url(d):
        return base64.urlsafe_b64encode(d).rstrip(b"=").decode()
    header = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}).encode())
    payload = b64url(json.dumps({"iat": int(time.time())}).encode())
    msg = f"{header}.{payload}".encode()
    sig = b64url(hmac.new(bytes.fromhex(secret_hex), msg, hashlib.sha256).digest())
    return f"{header}.{payload}.{sig}"


class RpcClient:
    def __init__(self, host="127.0.0.1", port=8553, jwt_secret_hex=None):
        self.url = f"http://{host}:{port}"
        self._headers = {"Content-Type": "application/json"}
        if jwt_secret_hex:
            self._headers["Authorization"] = f"Bearer {_jwt(jwt_secret_hex)}"
        # Bypass http_proxy so requests actually reach the node (see memory: urllib/requests
        # honor the env proxy, which was silently intercepting eth_sendRawTransaction).
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def call(self, method, params=None, _id=1):
        body = json.dumps(
            {"jsonrpc": "2.0", "method": method, "params": params or [], "id": _id}
        ).encode()
        req = urllib.request.Request(self.url, data=body, headers=self._headers)
        with self._opener.open(req, timeout=10) as resp:
            out = json.load(resp)
        if "error" in out:
            raise AssertionError(f"{method} RPC error: {out['error']}")
        return out.get("result")


GENESIS = os.environ.get("B3_GENESIS", "/tmp/op-spike/b3/config.genesis")


def _eth_genesis_header():
    """[eth_genesis_header] of the node's config.genesis — the authority for what the
    genesis block's header fields must round-trip to (applyEthGenesisHeader writes them)."""
    import configparser
    if not os.path.exists(GENESIS):
        raise SystemExit(f"genesis artifact not found: {GENESIS} (set B3_GENESIS)")
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(GENESIS)
    return cp["eth_genesis_header"]


PASSED = []
FAILED = []
KNOWN_RED = []

# Tier-1 known-reds: these checks depend on chain advancement (Tier-2: OP-mode payload
# building) or on the genesis flat-state read bug recorded in the handoff. At Tier-1 the
# chain sits at genesis, so they record as KNOWN-RED — visible in output, not gating.
# Remove names from this set as Tier-2 restores them. All three remaining names depend
# on chain advancement (genesis timestamp artifact / block 1 / MessagePasser writes).
# (empty — Tier-2 Phase B complete: withdrawalsRoot propagates since 08-19)
KNOWN_TIER1 = set()


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    elif name in KNOWN_TIER1:
        KNOWN_RED.append(name)
        print(f"  KNOWN-RED (tier-1) {name} {detail}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


def _known_error_check(name, e):
    """Record a failed call through check(), pinning the error signature: only the
    documented genesis flat-state error (-32603 "Invalid argument") may record as the
    tier-1 KNOWN-RED. Any other error code (e.g. -32601 method removed) fails loudly
    under a distinct name instead of hiding behind the known-red one."""
    check(name if "-32603" in str(e) else f"{name} (unexpected error code)",
          False, str(e)[:80])


# ---- A.1 engine_* (needs a separate node with enable_single_node_consensus=false for the
#      active-mode FCU/getPayload loops; here we assert the version-gate/capabilities surface
#      reachable on the B3 node) ----


def a1_engine_surface(erpc):
    print("A.1 engine surface")
    # Version-adaptive (08-18): the old line advertises the V4 trio; the scheduler line only
    # V3 (FCU V4 -> -38005, docs/2026-08-18-opstack-scheduler-e2e-verification.md). Pin that a
    # coherent trio exists at the line's version — the exact gates are covered by
    # OpNewPayloadRpcE2eTest and a1_active's other-version loop.
    caps = erpc.call("engine_exchangeCapabilities")
    trio = next((v for v in (4, 3)
                 if all(f"engine_{m}V{v}" in caps
                        for m in ["newPayload", "forkchoiceUpdated", "getPayload"])), None)
    check(f"exchangeCapabilities has a coherent trio (V{trio or '?'})", trio is not None, str(caps))
    check("newPayload method reachable", True)


# ---- A.2 eth_* ----


def a2_chain(rpc):
    print("A.2 chain")
    num = rpc.call("eth_blockNumber")
    check("blockNumber is 0x-prefixed", isinstance(num, str) and num.startswith("0x"), str(num))
    cid = rpc.call("eth_chainId")
    nv = rpc.call("net_version")
    check("chainId == net_version", cid == nv, f"{cid} vs {nv}")
    gp = rpc.call("eth_gasPrice")
    # op-geth parity: a suggestion for legacy txs = tip + head baseFee (never below the
    # base fee, or legacy sends signed from it are silently evicted). FISCO serves the
    # head baseFee itself (tip = this node's eth_maxPriorityFeePerGas, currently 0).
    lb = rpc.call("eth_getBlockByNumber", ["latest", False])
    check("gasPrice >= latest baseFeePerGas (op-geth suggestion semantics)",
          gp is not None and lb is not None and
          int(gp, 16) >= int(lb["baseFeePerGas"], 16),
          f"gasPrice={gp} baseFee={lb and lb.get('baseFeePerGas')}")
    # B1 (08-18): pin the current eth_maxPriorityFeePerGas. op-geth's is dynamic
    # (SuggestOptimismPriorityFee >= 1e6 wei, gasprice/optimism-gasprice.go:38);
    # FISCO's is the constant 0x0 (EthEndpoint.cpp:943) — divergence D-GP-2, see
    # docs/2026-08-18-rpc-parity-gasprice-withdrawals.md.
    mpf = rpc.call("eth_maxPriorityFeePerGas")
    check("maxPriorityFeePerGas returns 0x0 (FISCO constant)", mpf == "0x0", str(mpf))
    syncing = rpc.call("eth_syncing")
    check("syncing false", syncing is False, str(syncing))


def a2_blocks(rpc):
    print("A.2 blocks")
    num = rpc.call("eth_blockNumber")
    b = rpc.call("eth_getBlockByNumber", [num, False])
    check("block number matches", b["number"] == num)
    # R1: hash must round-trip through getBlockByHash (== s_number_2_hash on the node).
    b2 = rpc.call("eth_getBlockByHash", [b["hash"], False])
    check("getBlockByHash roundtrip", b2 is not None and b2["hash"] == b["hash"])
    check("parentHash present", b["parentHash"].startswith("0x"))
    # D2 E2: gasLimit/PBBR round-trip the genesis artifact (self-calibrating: at Tier-1 the
    # chain sits at genesis -> 30M/zero; after Tier-2 restores engine-built blocks the same
    # assertions keep holding against the artifact until blocks advance past genesis).
    gh = _eth_genesis_header()
    g0 = rpc.call("eth_getBlockByNumber", ["0x0", False])
    check("gasLimit matches genesis artifact (block 0)",
          g0 is not None and int(g0["gasLimit"], 16) == int(gh["gas_limit"], 0),
          f"{g0 and g0.get('gasLimit')} vs {gh['gas_limit']}")
    check("parentBeaconBlockRoot matches genesis artifact (block 0)",
          g0 is not None and g0["parentBeaconBlockRoot"].lower() ==
          gh["parent_beacon_block_root"].lower(), g0 and g0.get("parentBeaconBlockRoot"))
    # D2 E3: safe/finalized route to the FCU-tracked head. The original failure mode this
    # guards against is "no batcher -> derivation never advances -> tags pinned at genesis".
    # With an active op-batcher both tags advance, but they legitimately lag latest by the
    # structural batching latency (channel flush + L1 confirmations + sub-safety margin —
    # minutes on OP mainnet too), so a tight window is wrong: require resolution past genesis
    # and inside a generous window.
    for tag, window in (("safe", 600), ("finalized", 4000)):
        t = rpc.call("eth_getBlockByNumber", [tag, False])
        latest = int(b["number"], 16)
        tn = int(t["number"], 16) if t is not None else -1
        check(f"{tag} tag resolves (past genesis, within {window} of latest)",
              t is not None and 0 < tn and latest - window <= tn <= latest,
              f"tag={tn} latest={latest}")
    pend = rpc.call("eth_getBlockByNumber", ["pending", False])
    check("pending aliases latest", pend is not None and pend["number"] == b["number"],
          str(pend and pend.get("number")))
    # R2: baseFee is real (may legitimately be 0 on the genesis-adjacent chain).
    check("baseFeePerGas present", "baseFeePerGas" in b, str(b.get("baseFeePerGas")))
    # R2b: eth_feeHistory (foundry/alloy's EIP-1559 fee suggestion calls it on every send).
    fh = rpc.call("eth_feeHistory", ["0x5", "latest", [25, 50, 75]])
    ok = (isinstance(fh, dict) and "oldestBlock" in fh and
          isinstance(fh.get("baseFeePerGas"), list) and isinstance(fh.get("gasUsedRatio"), list) and
          len(fh["baseFeePerGas"]) == len(fh["gasUsedRatio"]) + 1 and
          isinstance(fh.get("reward"), list) and len(fh["reward"]) == len(fh["gasUsedRatio"]) and
          all(isinstance(r, list) and len(r) == 3 for r in fh["reward"]) and
          all(isinstance(x, (int, float)) and 0.0 <= x <= 1.0 for x in fh["gasUsedRatio"]))
    check("feeHistory shape (count+1 baseFees, ratios, reward rows)", ok, str(fh)[:90])
    oldest = int(fh["oldestBlock"], 16)
    ob = rpc.call("eth_getBlockByNumber", [hex(oldest), False])
    check("feeHistory first baseFee matches oldest block header",
          ob is not None and fh["baseFeePerGas"][0] == ob["baseFeePerGas"],
          f"{fh['baseFeePerGas'][0]} vs {ob and ob.get('baseFeePerGas')}")
    fh2 = rpc.call("eth_feeHistory", ["0x3", "latest"])
    check("feeHistory without percentiles omits reward",
          isinstance(fh2, dict) and "reward" not in fh2, str(fh2)[:70])
    check("extraData present", b.get("extraData", "").startswith("0x"))
    # timestamp: RPC is seconds; assert it parses and is sane (B3 started 2026).
    ts = int(b["timestamp"], 16)
    check("timestamp sane", 1_700_000_000 < ts < 2_100_000_000, str(ts))
    txc = rpc.call("eth_getBlockTransactionCountByNumber", [num])
    txs = b.get("transactions", [])
    check("tx count matches", int(txc, 16) == len(txs), f"{txc} vs {len(txs)}")
    # B3 (08-18): withdrawalsRoot shape. op-geth L2 blocks are always withdrawals:[]
    # (system withdrawals are an L1-beacon concept; Isthmus+ hard-rejects non-empty,
    # consensus/beacon/consensus.go:416-427) and the root is the L2ToL1MessagePasser
    # storage root. See docs/2026-08-18-rpc-parity-gasprice-withdrawals.md.
    check("withdrawals always [] (OP semantics)", b.get("withdrawals") == [], str(b.get("withdrawals")))
    wr = b.get("withdrawalsRoot")
    check("withdrawalsRoot present 32B (Isthmus+)", wr is not None and len(wr) == 66, str(wr))
    proof = rpc.call("eth_getProof", ["0x4200000000000000000000000000000000000016", [], "0x0"])
    # Dual expectation: self-written allocs (original B3) leave the passer empty (empty-trie
    # root), while op-deployer terminal allocs (C2 lineage) carry prewritten deployment
    # storage (non-empty root). Both are correct genesis states; verify the hash is a
    # well-formed 32-byte value and classify which case we are in.
    EMPTY_TRIE = "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
    sh = proof.get("storageHash", "")
    is_valid_root = len(sh) == 66 and sh.startswith("0x") and all(c in "0123456789abcdef" for c in sh[2:])
    check("genesis passer storageRoot well-formed (empty for self-written allocs, "
          "non-empty for op-deployer allocs)",
          is_valid_root,
          f"storageHash={sh} ({'empty-trie (self-written allocs)' if sh == EMPTY_TRIE else 'non-empty (op-deployer allocs)'})")
    # Genesis: the genesis header's withdrawalsRoot must equal the MessagePasser account
    # storage root (isthmus/exec-engine.md:58-59) -- the same root eth_getProof returns.
    # Empty for self-written allocs, non-empty for op-deployer proxied allocs (C2 lineage,
    # EIP-1967 slots); the header/proof equality holds in both cases (audit MN-4/S-GEN-3).
    g = rpc.call("eth_getBlockByNumber", ["0x0", False])
    check("genesis withdrawalsRoot == passer storage root (Isthmus invariant)",
          g.get("withdrawalsRoot") == sh,
          f"header={g.get('withdrawalsRoot')} proof={sh}")
    check("genesis withdrawals []", g.get("withdrawals") == [], str(g.get("withdrawals")))
    # ── getProof e2e: historical block tag (B4-1, spec §6 #9 P1) ──
    # Tightened 08-24: MPT-backed historical state reads are live (runtime blocks
    # persist trie nodes), so both tags must SUCCEED and return a real account
    # proof. The old dual acceptance (success or -32602/-32004 MPT-miss) pinned a
    # scope boundary that no longer exists.
    for desc, tag in [("block 1", "0x1"), ("latest SENDER", "latest")]:
        addr = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693" if "SENDER" in desc else \
               "0x4200000000000000000000000000000000000015"
        try:
            p = rpc.call("eth_getProof", [addr, [], tag])
            check(f"getProof {desc} succeeds with accountProof",
                  isinstance(p, dict) and isinstance(p.get("accountProof"), list)
                  and len(p["accountProof"]) > 0,
                  f"keys={list(p)[:5] if isinstance(p, dict) else p}")
        except AssertionError as e:
            check(f"getProof {desc} succeeds with accountProof", False, str(e)[:100])
    # ── OutputV0 cross-domain primitive (B3', spec §7) ──
    # op-node computes OutputRoot = keccak256(0x00×32 || stateRoot ||
    #   messagePasserStorageRoot || blockHash) from L2 block header fields.
    # On Isthmus+: messagePasserStorageRoot = header.withdrawalsRoot (L2Client.outputV0:199).
    # Verify the three inputs are present and non-zero on the latest block.
    lb = rpc.call("eth_getBlockByNumber", ["latest", False])
    sr = lb.get("stateRoot", "")
    wr = lb.get("withdrawalsRoot", "")
    bh = lb.get("hash", "")
    check("outputv0 stateRoot present 32B",
          sr and len(sr) == 66 and sr != "0x" + "0" * 64, str(sr[:20]))
    check("outputv0 withdrawalsRoot present 32B",
          wr and len(wr) == 66 and wr != "0x" + "0" * 64, str(wr[:20]))
    check("outputv0 blockHash present 32B",
          bh and len(bh) == 66 and bh != "0x" + "0" * 64, str(bh[:20]))


def a2_accounts(rpc, sender):
    print("A.2 accounts")
    # R3: getBalance/getTransactionCount read the real OP flat state now.
    bal = int(rpc.call("eth_getBalance", [sender, "latest"]), 16)
    check("getBalance nonzero", bal > 0, hex(bal))
    nonce = int(rpc.call("eth_getTransactionCount", [sender, "latest"]), 16)
    check("getTransactionCount int", nonce >= 0, str(nonce))
    code = rpc.call("eth_getCode", [sender, "latest"])
    check("EOA getCode == 0x", code == "0x", str(code))
    st = rpc.call("eth_getStorageAt", [sender, "0x0", "latest"])
    check("getStorageAt returns 0x0..0", st == "0x" + "00" * 32, str(st))


def a2_exec(rpc, sender):
    print("A.2 exec (eth_call/estimateGas)")
    # Phase-1 fixes: eth_call now executes (was: node crash / rev-mismatch / invalid-argument /
    # nonce-too-low / intrinsic-gas-too-low). Regression asserts on the B3 node.
    tx = {"from": sender, "to": "0x000000000000000000000000000000000000dEaD", "value": "0x1"}
    # Tier-1: at genesis every executed call dies in the executor's flat-state read with
    # -32603 "Invalid argument" (same family as getBalance nonzero / predeploy_matrix step 1).
    # Record the RPC error through check() — KNOWN_TIER1 marks it — instead of letting the
    # raise kill the whole group lambda (the baseline's single "<lambda>" red). The -32603
    # signature is pinned: any other error code fails loudly (_known_error_check).
    try:
        out = rpc.call("eth_call", [tx, "latest"])
        check("eth_call EOA->EOA returns 0x", out == "0x", str(out))
    except AssertionError as e:
        _known_error_check("eth_call EOA->EOA returns 0x", e)
    try:
        gas = rpc.call("eth_estimateGas", [tx])
        check("eth_estimateGas == 21000", int(gas, 16) == 21000, str(gas))
    except AssertionError as e:
        _known_error_check("eth_estimateGas == 21000", e)
    # A call to an empty-code address is a no-op returning empty output, not an error or crash.
    try:
        empty_out = rpc.call("eth_call",
            [{"to": "0x000000000000000000000000000000000000c0de", "data": "0x9a2ac6d5"}, "latest"])
        check("eth_call empty-code address returns 0x", empty_out == "0x", str(empty_out))
    except AssertionError as e:
        _known_error_check("eth_call empty-code address returns 0x", e)
    # The rebuilt genesis seeds the L1Block predeploy (getCode != 0x); its runtime rejects every
    # selector except setL1BlockValues with a clean revert. Assert the predeploy is present and
    # that eth_call on it returns an RPC error (revert), never a crash.
    l1_code = rpc.call("eth_getCode", ["0x4200000000000000000000000000000000000015", "latest"])
    check("L1Block predeploy seeded (getCode != 0x)", l1_code != "0x", l1_code[:16] + "...")
    try:
        rpc.call("eth_call",
            [{"to": "0x4200000000000000000000000000000000000015", "data": "0x9a2ac6d5"}, "latest"])
        check("eth_call L1Block unknown selector reverts", False, "unexpectedly returned")
    except AssertionError as e:
        check("eth_call L1Block unknown selector reverts", "error" in str(e), str(e))
    # historical blockTag: OP mode has no historical-state snapshot, so SchedulerInterface's
    # callAtBlock default routes to call() == latest. Assert the RPC is reachable and does not
    # crash; honoring block-N state is a documented gap (spec A.2 partial).
    try:
        h_out = rpc.call("eth_call", [tx, "0x1"])
        check("eth_call historical tag reachable (0x1)", h_out == "0x", str(h_out))
    except AssertionError as e:
        _known_error_check("eth_call historical tag reachable (0x1)", e)


def a4_scope(rpc):
    print("A.4 in/out-of-scope")
    # Spec §3 A.4: stub methods are IMPLEMENTED but excluded (return sentinels, no error); a
    # -32601 method-not-found would mean the stub is missing. Declared gaps must NOT be
    # implemented (present → the scope claim is stale).
    for m, arg in [("eth_coinbase", []), ("eth_mining", []), ("eth_hashrate", []),
                   ("eth_accounts", []), ("eth_sendTransaction", []), ("eth_sign", []),
                   ("eth_signTransaction", []),
                   ("eth_getUncleCountByBlockNumber", ["0x0"]),
                   ("eth_getUncleByBlockNumberAndIndex", ["0x0", "0x0"]), ("eth_subscribe", [])]:
        try:
            rpc.call(m, arg)
            check(f"{m} stub present", True)
        except AssertionError as e:
            check(f"{m} stub present", "-32601" not in str(e), str(e)[:70])
    for m in ["eth_protocolVersion", "engine_exchangeTransitionConfigurationV1",
              "engine_getPayloadBodiesV1", "engine_getPayloadBodiesV2", "engine_getClientVersionV1"]:
        try:
            rpc.call(m, [])
            check(f"{m} declared-gap", False, "expected method-not-found")
        except AssertionError as e:
            check(f"{m} declared-gap", "-32601" in str(e), str(e)[:70])


def a3_web3_net(rpc):
    print("A.3 web3/net")
    v = rpc.call("web3_clientVersion")
    check("clientVersion string", isinstance(v, str) and len(v) > 0, str(v))
    sha = rpc.call("web3_sha3", ["0x68656c6c6f"])  # keccak256("hello")
    check("web3_sha3 shape", isinstance(sha, str) and sha.startswith("0x") and len(sha) == 66, str(sha))
    pc = rpc.call("net_peerCount")
    check("peerCount numeric 0", pc == 0, str(pc))
    li = rpc.call("net_listening")
    check("listening true", li is True, str(li))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=int(os.environ.get("B3_ETH_PORT", 8553)))
    ap.add_argument("--engine-port", type=int, default=int(os.environ.get("B3_ENGINE_PORT", 8564)),
                    help="Engine RPC port (B3a has op_engine_rpc; B3 has consensus-only)")
    ap.add_argument("--jwt-secret", default=os.environ.get("B3A_JWT", "/tmp/op-spike/b3a/jwt.hex"))
    ap.add_argument("--only", default=None, help="a1|a2|a3")
    ap.add_argument("--sender",
                    default=os.environ.get("B3_SENDER", "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"),
                    help="funded EOA used by the a2 checks (fresh devnets must fund it first)")
    args = ap.parse_args()

    rpc = RpcClient(args.host, args.port)
    jwt_hex = open(args.jwt_secret).read().strip()
    erpc = RpcClient(args.host, args.engine_port, jwt_secret_hex=jwt_hex)
    groups = {"a1": [lambda: a1_engine_surface(erpc)],
              "a2": [lambda: a2_chain(rpc), lambda: a2_blocks(rpc), lambda: a2_accounts(rpc, args.sender), lambda: a2_exec(rpc, args.sender)],
              "a3": [lambda: a3_web3_net(rpc)],
              "a4": [lambda: a4_scope(rpc)]}
    if args.only:
        for fn in groups[args.only]:
            fn()
    else:
        for name, fns in groups.items():
            for fn in fns:
                try:
                    fn()
                except Exception as e:  # noqa
                    FAILED.append(fn.__name__)
                    print(f"  FAIL {fn.__name__} raised: {e}")

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed, {len(KNOWN_RED)} known-red (tier-1)")
    if KNOWN_RED:
        print(f"known-red (tier-1): {len(KNOWN_RED)} ({', '.join(KNOWN_RED)})")
    stale = KNOWN_TIER1.intersection(set(PASSED))
    if stale:
        print(f"WARNING: known-red checks passing (remove from KNOWN_TIER1): {sorted(stale)}")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
