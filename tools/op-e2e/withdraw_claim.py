#!/usr/bin/env python3
"""L1 withdrawal claim (prove + finalize) for the C2 devnet.

Closes the OP Stack withdraw loop against the FISCO L2: takes an L2
`initiateWithdrawal` tx hash, and walks the OptimismPortal2 (contracts 5.x,
index-based prove) flow on L1:

  1. Recover the MessagePassed event (nonce/sender/target/value/gasLimit/data,
     withdrawalHash) from the L2 receipt.
  2. Pick a finalized L2 block >= the withdrawal block and pull the output
     v0 proof from op-node (`optimism_outputAtBlock`); verify
     outputRoot == keccak(version|stateRoot|messagePasserStorageRoot|latestBlockHash).
  3. Pull the MessagePasser storage inclusion proof from the node's
     eth_getProof (the OP flat-state path rebuilds the trie from the committed
     flat state and gates on the block's stateRoot). Client-side trust
     anchors: the account proof must root at the live header's stateRoot, and
     the walked leaf must decode to the proven-in flag. Root equality against
     the committed messagePasserStorageRoot is checked as with the fallback.
     (--rocksdb-proof instead dumps the `/apps/<message-passer>` table
     straight from the L2 RocksDB via op_state_read and rebuilds the secure
     MPT locally — kept as a debugging bypass that needs node filesystem
     access.)
  4. Create a type-1 (permissioned) dispute game with rootClaim=outputRoot,
     extraData=uint256(l2Block) (this contracts version packs the claimed L2
     block number into extraData), bond 0.08 ETH.
  5. proveWithdrawalTransaction(_tx, gameIndex, outputRootProof, proof).
  6. Real-time waits on the devnet's compressed dispute timelines (setup
     intent: faultGameMaxClockDuration=60s, proofMaturityDelay=12s,
     disputeGameFinalityDelay=6s — op-e2e parity, op-e2e/config/init.go):
     wait out the game clock, resolveClaim(0,0)+resolve() (uncontested ->
     DEFENDER_WINS), wait maturity+finality, finalizeWithdrawalTransaction.
     NEVER warp the L1 clock instead: a warped clock permanently locks the
     sequencer into noTxPool catch-up ~30min later (handoff 裁决 8).

Every wait is real time on chain timestamps. Constants default to the C2
deployment (read from /tmp/c2/state.json) and are env-overridable:
C2_L2_WEB3 / C2_OP_NODE / C2_L1_RPC / C2_STATE / C2_ROCKSDB /
C2_PROPOSER_KEY / OP_STATE_READ. Requires: cast, python3 (rlp,
eth-hash, trie); --rocksdb-proof additionally needs op_state_read built.
"""
import argparse
import json
import os
import subprocess
import sys
import urllib.request

from eth_hash.auto import keccak
from trie.hexary import HexaryTrie
import rlp

# Every endpoint/path/key is env-overridable so the same tool runs against any
# devnet instance (C2 default, C3/c4 forks by export). Defaults = the C2 layout.
L2_WEB3 = os.environ.get("C2_L2_WEB3", "http://127.0.0.1:8555")
OP_NODE = os.environ.get("C2_OP_NODE", "http://127.0.0.1:9545")
L1 = os.environ.get("C2_L1_RPC", "http://127.0.0.1:8549")
STATE = os.environ.get("C2_STATE", "/tmp/c2/state.json")
ROCKSDB = os.environ.get("C2_ROCKSDB", "/tmp/c2/fisco/data/1/latest")
OP_STATE_READ = os.environ.get("OP_STATE_READ", "op_state_read")
MESSAGE_PASSER = "0x4200000000000000000000000000000000000016"
PROPOSER_KEY = os.environ.get(
    "C2_PROPOSER_KEY", "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d")  # DEV1

MESSAGE_PASSED_TOPIC = "0x02a52367d10742d8032712c1bb8e0144ff1ec5ffda1ed7d70bb05a2744955054"


def rpc(url, method, params):
    body = json.dumps({"jsonrpc": "2.0", "method": method, "params": params,
                       "id": 1}).encode()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(urllib.request.Request(
            url, body, {"Content-Type": "application/json"}), timeout=30) as r:
        out = json.load(r)
    if "error" in out:
        raise SystemExit(f"rpc {method} failed: {out['error']}")
    return out["result"]


def cast(*args):
    # Fail loud: a failed `cast send` writes to stderr and leaves stdout empty —
    # returning "" made every downstream print ("proven", "finalized: ?") a false
    # green during the 08-24 bring-up.
    r = subprocess.run(["cast", *args], capture_output=True, text=True)
    if r.returncode != 0 or (not r.stdout.strip() and r.stderr.strip()):
        raise SystemExit("cast " + " ".join(args[:2]) + " failed: " +
                         r.stderr.strip()[-400:])
    return r.stdout.strip()


def contracts():
    s = json.load(open(STATE))
    oc = s["opChainDeployments"][0]
    return oc["OptimismPortalProxy"], oc["DisputeGameFactoryProxy"]


def withdrawal_from_receipt(tx_hash):
    r = rpc(L2_WEB3, "eth_getTransactionReceipt", [tx_hash])
    for log in r["logs"]:
        if log["topics"][0] != MESSAGE_PASSED_TOPIC:
            continue
        t = log["topics"]
        data = log["data"][2:]
        words = [data[i:i + 64] for i in range(0, len(data), 64)]
        # Non-indexed args: value, gasLimit, data (dynamic), withdrawalHash. The data
        # tail starts at args_offset = int(words[2]) into the region AFTER the 4-word
        # head: [len][content]. words[4] is the LENGTH word, not the content — using it
        # as bytes (the old bug) silently substitutes 32 zero bytes for empty data,
        # which changes the L1-recomputed withdrawal hash and the proof walk dies
        # inside the portal's MerkleTrie ("invalid large internal hash").
        args = data[4 * 64:]
        off = int(words[2], 16) * 2
        dlen = int.from_bytes(bytes.fromhex(args[off:off + 64]), "big")
        content = args[off + 64:off + 64 + dlen * 2]
        return {
            "nonce": "0x" + t[1][2:],
            "sender": "0x" + t[2][26:],
            "target": "0x" + t[3][26:],
            "value": int(words[0], 16),
            "gasLimit": int(words[1], 16),
            "data": "0x" + content,
            "withdrawalHash": "0x" + words[3],
            "block": int(log["blockNumber"], 16),
        }
    raise SystemExit("MessagePassed not found in receipt")


def output_at_finalized(min_block, wait_seconds=0):
    import time
    deadline = time.time() + wait_seconds
    while True:
        fin = int(rpc(OP_NODE, "optimism_syncStatus", [])["finalized_l2"]["number"])
        if fin >= min_block:
            break
        if time.time() >= deadline:
            raise SystemExit(
                f"withdrawal block {min_block} not finalized within "
                f"{wait_seconds}s (finalized={fin})")
        time.sleep(15)
    res = rpc(OP_NODE, "optimism_outputAtBlock", [hex(fin)])
    proof = {
        "version": res["version"],
        "stateRoot": res["stateRoot"],
        "messagePasserStorageRoot": res["withdrawalStorageRoot"],
        "latestBlockHash": res["blockRef"]["hash"],
    }
    expected = "0x" + keccak(bytes.fromhex("".join(proof[k][2:] for k in
        ("version", "stateRoot", "messagePasserStorageRoot", "latestBlockHash")))).hex()
    if expected != res["outputRoot"]:
        raise SystemExit("outputRoot mismatch vs recomputed hash — refusing")
    return fin, res["outputRoot"], proof


def call_ok(*args):
    # cast-call exit status — True iff the simulation succeeds, i.e. a clock
    # gate (game clock, proof maturity, finality delay) has expired. A revert
    # here is an expected wait state, never an error.
    return subprocess.run(["cast", "call", *args],
                          capture_output=True).returncode == 0


def wait_until(desc, timeout, *args):
    # Poll a cast-call simulation until it succeeds or the timeout (seconds)
    # elapses. Timeouts are generous: they bound chain-clock waits, not checks.
    import time
    deadline = time.time() + timeout
    while time.time() < deadline:
        if call_ok(*args):
            return True
        time.sleep(5)
    print(f"!! {desc} not ready within {timeout}s")
    return False


def message_passer_slots():
    """Dump every storage slot of the MessagePasser from the L2 RocksDB."""
    tbl = "/apps/" + MESSAGE_PASSER[2:].lower()
    out = subprocess.run([OP_STATE_READ, "--help"], capture_output=True)
    listing = subprocess.run([sys.argv[0].replace("withdraw_claim.py",
        "op_state_list"), ROCKSDB, tbl, "200"], capture_output=True)
    import re
    keys = re.findall(r"key hex=([0-9a-f]+)",
                      listing.stdout.decode("utf-8", "replace"))
    # The physical key is "<table-ascii>:<raw 32B slot>"; in hex form the colon is byte 0x3a.
    prefix = tbl.encode().hex() + "3a"
    slots = []
    for k in keys:
        if not k.startswith(prefix):
            continue
        slot = k[len(prefix):]
        if len(slot) != 64 or slot in (
                "62616c616e6365", "6e6f6e6365", "636f646548617368"):
            continue  # balance/nonce/codeHash account fields
        val = subprocess.run([OP_STATE_READ, ROCKSDB, "hex:" + k],
                             capture_output=True, text=True).stdout.strip()
        slots.append((bytes.fromhex(slot), bytes.fromhex(val[4:])))
    return slots


def build_inclusion_proof(slots, storage_key):
    """Secure-MPT inclusion proof for storage_key, root-anchored."""
    db = {}
    t = HexaryTrie(db, prune=False)
    for slot, value in slots:
        if value == b"\x00" * 32:
            continue
        t[keccak(slot)] = rlp.encode(value.lstrip(b"\x00") or b"\x00")
    key = keccak(storage_key)
    proof_nodes = [rlp.encode(n) for n in t.get_proof(key)]
    val = HexaryTrie.get_from_proof(t.root_hash, key, list(t.get_proof(key)))
    if val != b"\x01":
        raise SystemExit(f"withdrawal slot not proven-in (value {val.hex()})")
    return t.root_hash, [p.hex() for p in proof_nodes]


def rpc_inclusion_proof(storage_key):
    """Inclusion proof for storage_key served by the node's eth_getProof.

    The OP flat-state path rebuilds the whole trie from the committed flat
    state and refuses (root gate) unless the rebuild matches the requested
    block's stateRoot — so the proof arrives already anchored. Client-side we
    still re-verify: the accountProof must hash-chain to the live header's
    stateRoot, and the walked storage leaf must be the proven-in flag (0x01).
    """
    header = rpc(L2_WEB3, "eth_getBlockByNumber", ["latest", False])
    res = rpc(L2_WEB3, "eth_getProof",
              [MESSAGE_PASSER, ["0x" + storage_key.hex()], "latest"])
    entry = res["storageProof"][0]
    acc = [rlp.decode(bytes.fromhex(n[2:])) for n in res["accountProof"]]
    state_root = bytes.fromhex(header["stateRoot"][2:])
    if keccak(rlp.encode(acc[0])) != state_root:
        raise SystemExit("eth_getProof accountProof not anchored at header stateRoot")
    root = bytes.fromhex(res["storageHash"][2:])
    nodes = [bytes.fromhex(n[2:]) for n in entry["proof"]]
    val = HexaryTrie.get_from_proof(root, keccak(storage_key),
                                    [rlp.decode(n) for n in nodes])
    if val != b"\x01":
        raise SystemExit(f"withdrawal slot not proven-in (value {val.hex() if val else 'none'})")
    return root, [n.hex() for n in nodes]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tx_hash", help="L2 tx hash of the initiateWithdrawal")
    ap.add_argument("--wait-finalized", type=int, default=0, metavar="SECONDS",
                    help="poll up to SECONDS for the finalized L2 head to pass "
                         "the withdrawal block before starting (CI: ~2400)")
    ap.add_argument("--rocksdb-proof", action="store_true",
                    help="build the storage proof from a local RocksDB dump "
                         "instead of the node's eth_getProof (debug bypass)")
    args = ap.parse_args()

    portal, dgf = contracts()
    w = withdrawal_from_receipt(args.tx_hash)
    print(f"withdrawal: {w['value'] / 1e18} ETH, block {w['block']}, "
          f"hash {w['withdrawalHash']}")

    l2_block, output_root, orp = output_at_finalized(
        w["block"], args.wait_finalized)
    print(f"output at finalized L2 {l2_block}: {output_root}")

    wh = bytes.fromhex(w["withdrawalHash"][2:])
    # Solidity mapping slot: sentMessages (slot 0) => keccak256(abi.encodePacked(key, slot)),
    # NOT keccak(rlp([key, ""])) — the RLP form lands on a key that is absent from the trie
    # (verified on C2: the encodePacked form matches the on-chain slot, value 0x01).
    storage_key = keccak(wh + b"\x00" * 32)
    if args.rocksdb_proof:
        slots = message_passer_slots()
        root, proof = build_inclusion_proof(slots, storage_key)
    else:
        root, proof = rpc_inclusion_proof(storage_key)
    if "0x" + root.hex() != orp["messagePasserStorageRoot"]:
        raise SystemExit(
            f"rebuilt storage root {root.hex()} != committed "
            f"{orp['messagePasserStorageRoot']} (storage dump incomplete?)")
    print(f"storage root match; proof has {len(proof)} nodes")

    # 1. dispute game (rootClaim=outputRoot, extraData=claimed L2 block)
    extra = "0x%064x" % l2_block
    out = cast("send", dgf, "create(uint32,bytes32,bytes)", "1", output_root,
               extra, "--value", "0.08ether", "--private-key", PROPOSER_KEY,
               "--rpc-url", L1, "--json")
    game_index = cast("call", dgf, "gameCount()(uint256)", "--rpc-url", L1)
    game_index = int(game_index.split()[0]) - 1
    print(f"game created, index {game_index}")
    game = cast("call", dgf,
                f"gameAtIndex(uint256)(uint32,uint64,address)", str(game_index),
                "--rpc-url", L1).split()[-1]

    # 2. prove
    tx = (f"({w['nonce']},{w['sender']},{w['target']},{w['value']},"
          f"{w['gasLimit']},{w['data']})")
    orps = "(" + ",".join(orp[k] for k in ("version", "stateRoot",
        "messagePasserStorageRoot", "latestBlockHash")) + ")"
    nodes = "[" + ",".join("0x" + n for n in proof) + "]"
    calldata = cast("calldata",
        '"proveWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes),uint256,(bytes32,bytes32,bytes32,bytes32),bytes[])"',
        f'"{tx}"', str(game_index), f'"{orps}"', f'"{nodes}"')
    cast("send", portal, "--data", calldata, "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    print("proven")

    # 3. real-time waits on the compressed devnet timelines (game clock 60s,
    #    maturity 12s, finality delay 6s — set in setup_c2.sh's intent). No
    #    clock warps: a warped L1 clock permanently locks the sequencer
    #    (handoff 裁决 8).
    if not wait_until("game clock (resolveClaim)", 360,
                      game, "resolveClaim(uint256,uint256)", "0", "0",
                      "--rpc-url", L1):
        raise SystemExit("resolveClaim not callable within 360s")
    cast("send", game, "resolveClaim(uint256,uint256)", "0", "0",
         "--private-key", PROPOSER_KEY, "--rpc-url", L1)
    cast("send", game, "resolve()", "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    print("game resolved (DEFENDER_WINS)")
    if not wait_until("proof maturity + finality delay", 300,
                      portal,
                      '"finalizeWithdrawalTransaction((uint256,address,address,'
                      'uint256,bytes))"', f'"{tx}"', "--rpc-url", L1):
        raise SystemExit("finalize not callable within 300s")
    fin = cast("send", portal,
        '"finalizeWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes))"', f'"{tx}"', "--private-key", PROPOSER_KEY,
        "--rpc-url", L1)
    print("finalized:", fin.splitlines()[0] if fin else "?")


if __name__ == "__main__":
    main()
