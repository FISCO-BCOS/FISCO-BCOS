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

# cast (alloy/reqwest) honors the macOS SYSTEM proxy; a local VPN/proxy tool
# then intercepts 127.0.0.1 RPC calls and answers with unparseable bodies
# ("Error: parser error:"). Everything here talks to localhost only.
os.environ.setdefault("NO_PROXY", "127.0.0.1,localhost")
os.environ.setdefault("no_proxy", "127.0.0.1,localhost")

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
# Address of PROPOSER_KEY. finalize's gate reads provenWithdrawals[hash][msg.sender],
# so cast-call simulations MUST pass --from this address — a zero-address caller
# always observes the withdrawal as unproven (0xcca6afda) and the wait never ends.
PROPOSER_ADDR = os.environ.get(
    "C2_PROPOSER_ADDR", "0x70997970C51812dc3A010C7d01b50e0d17dc79C8")

# The dispute-game counterparty. Game type 1 is a PermissionedDisputeGame: only
# the intent-registered (proposer, challenger) may move — the challenger role
# must be a DIFFERENT account than the proposer for the adversarial scenarios
# (setup_c2.sh registers DEV0 as the challenger).
CHALLENGER_KEY = os.environ.get(
    "C2_CHALLENGER_KEY", "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80")
CHALLENGER_ADDR = os.environ.get(
    "C2_CHALLENGER_ADDR", "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266")
# Claim-commitment constants for the clock-driven contest scenarios: the game
# validates positions/bonds, not claim contents, before resolution.
FAKE_ROOT = "0x" + "66" * 32
COUNTER_CLAIM = "0x" + "01" * 32
REBUT_CLAIM = "0x" + "02" * 32
# GameStatus (contracts-bedrock Types.sol): 0=IN_PROGRESS 1=CHALLENGER_WINS
# 2=DEFENDER_WINS
STATUS_CHALLENGER_WINS, STATUS_DEFENDER_WINS = 1, 2

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


def decode_message_passed(log):
    """Decode one MessagePassed log into the eight reported fields (pure).

    Non-indexed args: value, gasLimit, data (dynamic), withdrawalHash. The
    data tail starts at args_offset BYTES from the START of the data blob
    (ABI offsets are blob-relative — the historical bug double-counted the
    head by slicing `args` with the blob offset, silently decoding every
    non-empty data to ""; found by review 2026-08-23). words[4] is the
    LENGTH word, not the content — using it as bytes (the older bug)
    substitutes 32 zero bytes for empty data and diverges the L1-recomputed
    withdrawal hash inside the portal's MerkleTrie.
    """
    t = log["topics"]
    data = log["data"][2:]
    words = [data[i:i + 64] for i in range(0, len(data), 64)]
    off = int(words[2], 16) * 2              # chars from the data blob start
    dlen = int.from_bytes(bytes.fromhex(data[off:off + 64]), "big")
    content = data[off + 64:off + 64 + dlen * 2]
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


def withdrawal_from_receipt(tx_hash):
    r = rpc(L2_WEB3, "eth_getTransactionReceipt", [tx_hash])
    for log in r["logs"]:
        if log["topics"][0] != MESSAGE_PASSED_TOPIC:
            continue
        return decode_message_passed(log)
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
    # Deprecated shim — the implementation moved to c2lib (one home for the
    # family's polling idioms); kept so external callers keep working.
    import c2lib
    return c2lib.call_ok(*args)


def wait_until(desc, timeout, *args):
    # Shim over c2lib.wait_until (see c2lib.py for the fail-fast contract).
    import c2lib
    return c2lib.wait_until(desc, timeout, *args)


def lifecycle(desc, *args, contains=None, nonzero=False, equals=None):
    """Read-only state-machine assertion between claim-flow steps.

    Runs `cast call` and asserts the view matches the expected lifecycle
    stage: `contains` requires the (case-insensitive) substring in the output,
    `nonzero` requires the first integer to be > 0 (createdAt / resolvedAt
    style checks). Failing loud beats a downstream cryptic revert — the state
    machine this walks is exactly what the portal's gate errors encode.
    """
    import re, subprocess
    r = subprocess.run(["cast", "call", *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"lifecycle assert failed: {desc} (call reverted)\n"
                         f"  argv: cast call {' '.join(map(str, args))}\n"
                         f"  stderr: {(r.stderr or '').strip()[:300]}")
    out = r.stdout
    # cast never prints the "[N]" tuple form for single-value returns —
    # normalize that pattern (e.g. contains="[0]") into a value comparison
    # so every numeric lifecycle assert means what it says.
    if contains is not None and re.fullmatch(r"\[\d+\]", str(contains)):
        equals, contains = int(str(contains)[1:-1]), None
    if equals is not None:
        # cast prints uint8 returns bare ("0") or hex ("0x00") — never the
        # "[0]" tuple form; compare the parsed value.
        import re as _re
        m = _re.search(r"0x[0-9a-fA-F]+|\d+", out)
        if not m:
            raise SystemExit(f"lifecycle assert failed: {desc}\n"
                             f"  no numeric value in: {out.strip()[:200]}")
        got = int(m.group(0), 16) if m.group(0).lower().startswith("0x") else int(m.group(0))
        if got != equals:
            raise SystemExit(f"lifecycle assert failed: {desc}\n"
                             f"  expected {equals}, got {got}")
    if contains is not None and contains.lower() not in out.lower():
        raise SystemExit(f"lifecycle assert failed: {desc}\n"
                         f"  expected to contain: {contains}\n  got: {out.strip()[:200]}")
    if nonzero:
        m = re.search(r"\d+", out)
        if not m or int(m.group(0)) == 0:
            raise SystemExit(f"lifecycle assert failed: {desc} (expected nonzero, "
                             f"got: {out.strip()[:200]})")
    print(f"  [lifecycle] {desc}")


def rocksdb_table_prefix(table):
    """Physical-key prefix of a table's rows in the RocksDB dump: the table
    path + ':' (byte 0x3a), hex-encoded (historical bug: a literal ":" prefix
    missed every physical key)."""
    return table.encode().hex() + "3a"


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
    prefix = rocksdb_table_prefix(tbl)
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


def message_passer_storage_key(withdrawal_hash):
    """Solidity mapping slot: sentMessages (slot 0) =>
    keccak256(abi.encodePacked(key, slot)), NOT keccak(rlp([key, ""])) — the
    RLP form lands on a key absent from the trie (verified on C2: the
    encodePacked form matches the on-chain slot, value 0x01)."""
    return keccak(withdrawal_hash + b"\x00" * 32)


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


def create_game(dgf, root_claim, l2_block):
    """create(type=1, rootClaim, extraData=l2Block) + resolve the game address."""
    extra = "0x%064x" % l2_block
    cast("send", dgf, "create(uint32,bytes32,bytes)", "1", root_claim,
         extra, "--value", "0.08ether", "--private-key", PROPOSER_KEY,
         "--rpc-url", L1, "--json")
    game_index = cast("call", dgf, "gameCount()(uint256)", "--rpc-url", L1)
    game_index = int(game_index.split()[0]) - 1
    print(f"game created, index {game_index}")
    game = cast("call", dgf,
                f"gameAtIndex(uint256)(uint32,uint64,address)", str(game_index),
                "--rpc-url", L1).split()[-1]
    return game_index, game


def prove_withdrawal(portal, w, game_index, orp, proof):
    """prove + the creation-block wait (same-block proves revert with
    InvalidProofTimestamp). Bare args — cast 1.7.1 rejects quoted forms."""
    tx = (f"({w['nonce']},{w['sender']},{w['target']},{w['value']},"
          f"{w['gasLimit']},{w['data']})")
    orps = "(" + ",".join(orp[k] for k in ("version", "stateRoot",
        "messagePasserStorageRoot", "latestBlockHash")) + ")"
    nodes = "[" + ",".join("0x" + n for n in proof) + "]"
    calldata = cast("calldata",
        'proveWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes),uint256,(bytes32,bytes32,bytes32,bytes32),bytes[])',
        tx, str(game_index), orps, nodes)
    if not wait_until("prove (game-creation block passed)", 120,
                      portal, "--data", calldata, "--from", PROPOSER_ADDR,
                      "--rpc-url", L1):
        raise SystemExit("prove not callable within 120s")
    cast("send", portal, "--data", calldata, "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    return tx, calldata


def game_status(game):
    raw = cast("call", game, "status()(uint8)",
               "--rpc-url", L1).split()[0]
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw)


def game_credit(game, who):
    """Bond ledger view (wei-exact, no gas noise). claimCredit itself is
    unusable in an e2e: the first call only unlocks, and DelayedWETH holds the
    withdrawal behind a 3.5-day delay."""
    raw = cast("call", game, "credit(address)(uint256)", who,
               "--rpc-url", L1).split()[0]
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw)


def required_bond_wei(game, gindex):
    """Big-Bonds depth ladder: the root attack child sits at gindex 2, its
    attack child at 4 (a 0.08 flat bond IncorrectBondAmount-reverts there)."""
    raw = cast("call", game, "getRequiredBond(uint128)", str(gindex),
               "--rpc-url", L1).split()[0]
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw)


def contest_abandoned(output_root, w, orp, proof, portal, dgf, tx, l2_block):
    """Honest claim challenged, then the challenger goes silent -> DEFENDER_WINS.

    Bottom-up resolution is mandatory (OutOfOrderResolution otherwise): the
    rebut leaf (claim 2) resolves first, then the challenger's claim 1, then
    the root — percolation resets root.counteredBy to 0 -> DEFENDER_WINS.
    """
    print("== contest: abandoned (challenger attacks the root, then silence)")
    game_index, game = create_game(dgf, output_root, l2_block)
    prove_withdrawal(portal, w, game_index, orp, proof)
    print("proven (honest claim)")
    root_bond = required_bond_wei(game, 2)
    cast("send", game, "attack(bytes32,uint256,bytes32)",
         output_root, "0", COUNTER_CLAIM, "--value", f"{root_bond}wei",
         "--private-key", CHALLENGER_KEY, "--rpc-url", L1)
    print(f"challenger countered the root (bond {root_bond} wei)")
    rebut_bond = required_bond_wei(game, 4)
    cast("send", game, "attack(bytes32,uint256,bytes32)",
         COUNTER_CLAIM, "1", REBUT_CLAIM, "--value", f"{rebut_bond}wei",
         "--private-key", PROPOSER_KEY, "--rpc-url", L1)
    print(f"defender countered the challenger's claim (bond {rebut_bond} wei)")
    # Challenger silence: their side's clock on the rebut (claim 2) expires,
    # then the subgames collapse bottom-up.
    for label, idx, num in (("rebut leaf (claim 2)", "2", "0"),
                            ("challenger claim (claim 1)", "1", "1"),
                            ("root (claim 0)", "0", "1")):
        if not wait_until(f"clock + resolution ({label})", 360, game,
                          "resolveClaim(uint256,uint256)", idx, num,
                          "--rpc-url", L1):
            raise SystemExit(f"{label} not resolvable within 360s")
        cast("send", game, "resolveClaim(uint256,uint256)", idx, num,
             "--private-key", PROPOSER_KEY, "--rpc-url", L1)
    cast("send", game, "resolve()", "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    status = game_status(game)
    if status != STATUS_DEFENDER_WINS:
        raise SystemExit(f"expected DEFENDER_WINS(2), got {status}")
    print("game resolved: DEFENDER_WINS under an abandoned challenge")
    # Bond ledger: defender recovers its own bonds AND pockets the challenger's
    # forfeited root-attack bond; the challenger's credit stays zero.
    dev1_credit = game_credit(game, PROPOSER_ADDR)
    dev0_credit = game_credit(game, CHALLENGER_ADDR)
    expected = 80_000_000_000_000_000 + root_bond + rebut_bond  # root+claim1+claim2
    if dev1_credit != expected:
        raise SystemExit(f"defender credit {dev1_credit} != expected {expected}")
    if dev0_credit != 0:
        raise SystemExit(f"challenger credit {dev0_credit} != 0")
    print(f"bond ledger: defender credit {dev1_credit} wei, challenger 0")
    wtx = (f"({w['nonce']},{w['sender']},{w['target']},{w['value']},"
           f"{w['gasLimit']},{w['data']})")
    if not wait_until("proof maturity + finality delay", 600,
                      portal,
                      'finalizeWithdrawalTransaction((uint256,address,address,'
                      'uint256,uint256,bytes))', wtx,
                      "--from", PROPOSER_ADDR, "--rpc-url", L1):
        raise SystemExit("finalize not callable within 600s")
    cast("send", portal,
         'finalizeWithdrawalTransaction((uint256,address,address,uint256,'
         'uint256,bytes))', wtx, "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    print("finalized: honest claim stays finalizable under an abandoned challenge")


def contest_dishonest(w, orp, proof, portal, dgf, tx, l2_block):
    """Proposer posts a FAKE output root; a real proof provably cannot prove
    against it; challenged and silent -> CHALLENGER_WINS, bond forfeited."""
    print("== contest: dishonest (proposer posts a FAKE output root)")
    # extraData must clear the anchor state left by any earlier resolved game
    # (initialize: UnexpectedRootClaim) — claim one block past it.
    game_index, game = create_game(dgf, FAKE_ROOT, l2_block + 1)
    # Game-side verification oracle: wait past the creation-block timestamp
    # gate (InvalidProofTimestamp fires first in the same block), then a REAL
    # withdrawal proof must die on the root-claim binding: the portal binds the
    # proof to game.rootClaim() — the fake root can never match. cast prints
    # the revert data; assert the selector so this is not a gas-style false red.
    real_tx = (f"({w['nonce']},{w['sender']},{w['target']},{w['value']},"
               f"{w['gasLimit']},{w['data']})")
    orps = "(" + ",".join(orp[k] for k in ("version", "stateRoot",
        "messagePasserStorageRoot", "latestBlockHash")) + ")"
    nodes = "[" + ",".join("0x" + n for n in proof) + "]"
    calldata = cast("calldata",
        'proveWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes),uint256,(bytes32,bytes32,bytes32,bytes32),bytes[])',
        real_tx, str(game_index), orps, nodes)
    deadline_sel = "0x426149af"  # OptimismPortal_InvalidOutputRootProof()
    import time as _time
    deadline = _time.time() + 120
    while _time.time() < deadline:
        r = subprocess.run(["cast", "call", portal, "--data", calldata,
                            "--from", PROPOSER_ADDR, "--rpc-url", L1],
                           capture_output=True, text=True)
        if r.returncode != 0:
            out = r.stdout + r.stderr
            if "0xb4caa4e5" in out:  # InvalidProofTimestamp: same-block gate first
                _time.sleep(3)
                continue
            if deadline_sel in out:
                print("prove against the fake root reverts with "
                      "InvalidOutputRootProof — game-side verification holds")
                break
            raise SystemExit(f"prove reverted with an UNEXPECTED reason "
                             f"(wanted {deadline_sel}): {out[-300:]}")
        raise SystemExit("prove against a FAKE root SUCCEEDED — game "
                         "verification is broken")
    else:
        raise SystemExit("prove precheck never reached the root-claim check "
                         "within 120s")
    root_bond = required_bond_wei(game, 2)
    cast("send", game, "attack(bytes32,uint256,bytes32)",
         FAKE_ROOT, "0", COUNTER_CLAIM, "--value", f"{root_bond}wei",
         "--private-key", CHALLENGER_KEY, "--rpc-url", L1)
    print(f"challenger countered the fake root (bond {root_bond} wei)")
    # The lying proposer stays silent: bottom-up resolution, CHALLENGER_WINS.
    for label, idx, num in (("challenger claim (claim 1)", "1", "0"),
                            ("root (claim 0)", "0", "1")):
        if not wait_until(f"clock + resolution ({label})", 360, game,
                          "resolveClaim(uint256,uint256)", idx, num,
                          "--rpc-url", L1):
            raise SystemExit(f"{label} not resolvable within 360s")
        cast("send", game, "resolveClaim(uint256,uint256)", idx, num,
             "--private-key", CHALLENGER_KEY, "--rpc-url", L1)
    cast("send", game, "resolve()", "--private-key", CHALLENGER_KEY,
         "--rpc-url", L1)
    status = game_status(game)
    if status != STATUS_CHALLENGER_WINS:
        raise SystemExit(f"expected CHALLENGER_WINS(1), got {status}")
    print("game resolved: CHALLENGER_WINS — the liar loses")
    dev0_credit = game_credit(game, CHALLENGER_ADDR)
    dev1_credit = game_credit(game, PROPOSER_ADDR)
    expected = 80_000_000_000_000_000 + root_bond  # forfeited root + own refund
    if dev0_credit != expected:
        raise SystemExit(f"challenger credit {dev0_credit} != expected {expected}")
    if dev1_credit != 0:
        raise SystemExit(f"lying proposer credit {dev1_credit} != 0")
    print(f"bond ledger: challenger credit {dev0_credit} wei, liar 0")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tx_hash", help="L2 tx hash of the initiateWithdrawal")
    ap.add_argument("--wait-finalized", type=int, default=0, metavar="SECONDS",
                    help="poll up to SECONDS for the finalized L2 head to pass "
                         "the withdrawal block before starting (CI: ~2400)")
    ap.add_argument("--rocksdb-proof", action="store_true",
                    help="build the storage proof from a local RocksDB dump "
                         "instead of the node's eth_getProof (debug bypass)")
    ap.add_argument("--contest", choices=["abandoned", "dishonest"], default=None,
                    help="adversarial dispute scenarios (requires the intent "
                         "challenger role to differ from the proposer — DEV0 "
                         "on devnets built by the current setup_c2.sh): "
                         "'abandoned' = honest claim challenged then the "
                         "challenger goes silent -> DEFENDER_WINS, still "
                         "finalizable; 'dishonest' = proposer posts a FAKE "
                         "output root -> a real proof provably reverts against "
                         "it -> CHALLENGER_WINS, bond forfeited")
    args = ap.parse_args()

    portal, dgf = contracts()
    w = withdrawal_from_receipt(args.tx_hash)
    print(f"withdrawal: {w['value'] / 1e18} ETH, block {w['block']}, "
          f"hash {w['withdrawalHash']}")

    l2_block, output_root, orp = output_at_finalized(
        w["block"], args.wait_finalized)
    print(f"output at finalized L2 {l2_block}: {output_root}")

    wh = bytes.fromhex(w["withdrawalHash"][2:])
    storage_key = message_passer_storage_key(wh)
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

    if args.contest == "abandoned":
        contest_abandoned(output_root, w, orp, proof, portal, dgf, args.tx_hash, l2_block)
        return
    if args.contest == "dishonest":
        contest_dishonest(w, orp, proof, portal, dgf, args.tx_hash, l2_block)
        return

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
    lifecycle("game IN_PROGRESS after create",
              game, "status()(uint8)", "--rpc-url", L1, equals=0)
    lifecycle("game createdAt set",
              game, "createdAt()(uint64)", "--rpc-url", L1, nonzero=True)

    # 2. prove
    tx = (f"({w['nonce']},{w['sender']},{w['target']},{w['value']},"
          f"{w['gasLimit']},{w['data']})")
    orps = "(" + ",".join(orp[k] for k in ("version", "stateRoot",
        "messagePasserStorageRoot", "latestBlockHash")) + ")"
    nodes = "[" + ",".join("0x" + n for n in proof) + "]"
    # cast 1.7.1 rejects signatures/args wrapped in literal double quotes (an
    # older cast tolerated them) — pass everything bare.
    calldata = cast("calldata",
        'proveWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes),uint256,(bytes32,bytes32,bytes32,bytes32),bytes[])',
        tx, str(game_index), orps, nodes)
    # The portal rejects a proof in the same L1 block as the game's creation
    # (OptimismPortal_InvalidProofTimestamp: block.timestamp <= createdAt) —
    # poll the simulation until the chain moves past the creation block.
    if not wait_until("prove (game-creation block passed)", 120,
                      portal, "--data", calldata, "--rpc-url", L1):
        raise SystemExit("prove not callable within 120s")
    cast("send", portal, "--data", calldata, "--private-key", PROPOSER_KEY,
         "--rpc-url", L1)
    print("proven")
    lifecycle("portal records the proof (game + timestamp)",
              portal, "provenWithdrawals(bytes32,address)(address,uint64)",
              w["withdrawalHash"], PROPOSER_ADDR, "--rpc-url", L1, contains=game)

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
    lifecycle("game DEFENDER_WINS after resolve",
              game, "status()(uint8)", "--rpc-url", L1, equals=2)
    lifecycle("game resolvedAt set",
              game, "resolvedAt()(uint64)", "--rpc-url", L1, nonzero=True)
    if not wait_until("proof maturity + finality delay", 600,
                      portal,
                      'finalizeWithdrawalTransaction((uint256,address,address,'
                      'uint256,uint256,bytes))', tx,
                      "--from", PROPOSER_ADDR, "--rpc-url", L1):
        raise SystemExit("finalize not callable within 300s")
    fin = cast("send", portal,
        'finalizeWithdrawalTransaction((uint256,address,address,uint256,'
        'uint256,bytes))', tx, "--private-key", PROPOSER_KEY,
        "--rpc-url", L1)
    print("finalized:", fin.splitlines()[0] if fin else "?")
    lifecycle("finalizedWithdrawals[wh] == true",
              portal, "finalizedWithdrawals(bytes32)(bool)",
              w["withdrawalHash"], "--rpc-url", L1, contains="true")

    # 4. bond recovery: the 0.08 ETH create-bond is claimable via a TWO-STEP
    #    claimCredit — the first call unlocks (starts the DelayedWETH countdown,
    #    WETH_UNLOCK_SECONDS from the setup intent), the second pays it out.
    #    The unlock flag (hasUnlockedCredit) flips on the first SEND, so the
    #    wait simulates the SECOND call: before the delay expires the inner
    #    weth.withdraw reverts ("withdrawal delay not met"), after it the whole
    #    path succeeds and the payout send can follow.
    cast("send", game, "claimCredit(address)", PROPOSER_ADDR,
         "--private-key", PROPOSER_KEY, "--rpc-url", L1)
    print("bond unlocked (DelayedWETH countdown started)")
    if not wait_until("bond unlock delay", 120,
                      game, "claimCredit(address)", PROPOSER_ADDR,
                      "--from", PROPOSER_ADDR, "--rpc-url", L1):
        raise SystemExit("bond not claimable within 120s")
    cast("send", game, "claimCredit(address)", PROPOSER_ADDR,
         "--private-key", PROPOSER_KEY, "--rpc-url", L1)
    print("bond recovered (0.08 ETH back to the proposer)")
    lifecycle("game credit drained after recovery",
              game, "credit(address)(uint256)", PROPOSER_ADDR,
              "--rpc-url", L1, contains="[0]")


if __name__ == "__main__":
    main()
