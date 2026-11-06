#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""op-genesis-to-allocs.py — convert an op-geth genesis JSON into the
FISCO-BCOS config.genesis fragment for the OP (L2) lane.

Input: a standard op-geth / geth genesis JSON (as produced by `op-geth init`
dumps, op-deployer, or the ethereum-optimism/superchain-registry genesis
artifacts). Output: a single INI fragment carrying

  * [alloc.N] / [alloc.N.storage] sections for every genesis account
  * [eth_genesis_header]  — the full Ethereum B0 header (22 keys)
  * [op_fork_timestamps]  — the OP fork schedule (regolith_time .. karst_time)

state_root and the header hash are recomputed from the accounts and the header
fields the way op-geth's Genesis.ToBlock does (fork-gated fields the JSON omits
are derived, see derive_fork_fields).

This tool never creates accounts. Chain-specific predeploys (the bcos-l2-contracts
contracts, or any other) go into the genesis the OP way, as plain alloc entries: the
harness's tools/opstack-genesis/build-allocs.py in FISCO-BCOS/op-stack-e2e-tests merges
them onto the op-deployer genesis, and its --out-json merged alloc is passed here with
--alloc (tools/op-genesis-repro.sh does both steps). --alloc must keep every --genesis
account unchanged and may add accounts; the merged state_root and hash are then emitted
and rollup.json must be updated to that hash. A hash from --genesis-hash / rollup.json
genesis.l2.hash (and a stateRoot in the JSON) must equal what the --genesis accounts
produce, else the tool aborts.

Requires the python packages eth-hash, rlp and trie
(tools/.ci/c2-e2e-requirements.txt).

The emitted format is pinned to the C++ parsers in
bcos-tool/bcos-tool/NodeConfig.cpp:

  * loadAllocs (NodeConfig.cpp:270-362)
      address        REQUIRED, 0x-prefixed, exactly 40 lowercase hex chars
      balance        decimal digits (default "0"), u256
      nonce          decimal digits (default "0"), must fit uint64
      code           optional; absent/empty = EOA; else 0x-prefixed even hex
      [alloc.N.storage]  key=value, both 0x-prefixed, exactly 64 hex chars
  * loadEthGenesisHeader (NodeConfig.cpp:364-492)
      16 required keys, all 0x-prefixed hex; quantities are 0x QUANTITY
      (minimal form, "0x0" for zero); number must be 0x0; the six fork-gated
      keys (base_fee_per_gas / withdrawals_root / blob_gas_used /
      excess_blob_gas / parent_beacon_block_root / requests_hash) are emitted
      only when the JSON carries them — an absent key makes the ledger
      re-encode the header WITHOUT that RLP field, which is what keeps the
      hash byte-exact.
  * loadOpForkTimestamps (NodeConfig.cpp:1380-1496)
      ten OPTIONAL keys (regolith_time .. karst_time), decimal or 0x-hex; an
      absent key means "not scheduled" (UINT64_MAX on the C++ side). Unknown
      keys are REJECTED by the parser, so this tool only emits the keys the
      current parser accepts — see ACCEPTED_OP_FORK_KEYS below. Two C++
      invariants are mirrored here: scheduled times must be non-decreasing
      down the ladder, and scheduling a pre-Isthmus fork requires
      isthmus_time to be set (NodeConfig.cpp:1462-1482).

Ledger::buildGenesisBlock recomputes the state root from [alloc.*] and the
hash from the header fields and refuses to boot on mismatch;
`eth-sync-check --genesis-ini <config.genesis>` runs the same checks offline.

Usage:
    python3 tools/op-genesis-to-allocs.py --genesis genesis.json [--alloc allocs.json] \\
        [--rollup rollup.json] [--genesis-hash 0x...] [--name op-sepolia] \\
        [--timestamp] -o config.genesis.fragment.ini

Fork times are taken from --rollup (op-node's rollup.json: snake_case
`*_time` keys, plus genesis.l2.hash / genesis.l2_time) when given, else from
the genesis JSON's `config` object (camelCase `*Time` keys). null/absent
times are omitted from the output (= never activates).

OP Sepolia example (chainId=11155420, genesis hash
0x102de6ffb001480cc9b8b548fd05c34cd4f46ae4aa91759393db90ea0409887d):

    # genesis JSON (zstd + shared dictionary) from the superchain-registry
    # (https://github.com/ethereum-optimism/superchain-registry):
    curl -L -o op.json.zst https://raw.githubusercontent.com/\
ethereum-optimism/superchain-registry/main/superchain/extra/genesis/sepolia/op.json.zst
    curl -L -o dictionary https://raw.githubusercontent.com/\
ethereum-optimism/superchain-registry/main/superchain/extra/dictionary
    zstd -d -D dictionary op.json.zst -o op-genesis.json
    # alternatively: op-geth --op-sepolia dumpgenesis > op-genesis.json

    python3 tools/op-genesis-to-allocs.py --genesis op-genesis.json \\
        --rollup rollup.json --name op-sepolia -o op-sepolia.genesis.ini

"""
import argparse
import datetime
import json
import sys
from pathlib import Path

# Canonical Ethereum constants (match tools/BcosBuilder/src/tpl/config.genesis.el).
EMPTY_TRIE_ROOT = "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
EMPTY_UNCLES_HASH = "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"
ZERO_HASH = "0x" + "00" * 32
ZERO_ADDRESS = "0x" + "00" * 20
ZERO_BLOOM = "0x" + "00" * 256

# EIP-7685 empty requests hash, sha256(""): op-geth Genesis.ToBlock sets it from Prague on.
EMPTY_REQUESTS_HASH = "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
INITIAL_BASE_FEE = 1000000000  # params.InitialBaseFee: London genesis without baseFeePerGas
# Isthmus+ genesis: withdrawalsRoot = this predeploy's storage root (op-geth core/genesis.go).
L2_TO_L1_MESSAGE_PASSER = 0x4200000000000000000000000000000000000016

UINT64_MAX = (1 << 64) - 1
UINT256_MAX = (1 << 256) - 1
# loadEthGenesisHeader stores timestamp*1000 in an int64 (NodeConfig.cpp:430-438).
MAX_TIMESTAMP = (1 << 63) - 1
MAX_TIMESTAMP_SEC = MAX_TIMESTAMP // 1000

# ---------------------------------------------------------------------------
# OP fork-time field mapping. The full OP fork ladder is listed so that a
# parser extension only needs a set change in ACCEPTED_OP_FORK_KEYS.
#   ini key          genesis.json config key (camelCase)   rollup.json key
# IMPORTANT: NodeConfig::loadOpForkTimestamps (NodeConfig.cpp:1414-1426)
# rejects any key it does not know. As of this writing the parser accepts the
# full ten-key ladder regolith..karst (Bedrock is the genesis fork and has no
# key); isthmus_time also doubles as the ladder-mode switch — without it
# Isthmus is the zero-start baseline and the pre-Isthmus rungs are never
# consulted (NodeConfig.cpp:1462-1482), which this tool mirrors as a hard
# error.
# ---------------------------------------------------------------------------
OP_FORK_LADDER = [
    ("regolith_time", "regolithTime"),
    ("canyon_time", "canyonTime"),
    ("delta_time", "deltaTime"),
    ("ecotone_time", "ecotoneTime"),
    ("fjord_time", "fjordTime"),
    ("granite_time", "graniteTime"),
    ("holocene_time", "holoceneTime"),
    ("isthmus_time", "isthmusTime"),
    ("jovian_time", "jovianTime"),
    ("karst_time", "karstTime"),
]
# Keys the CURRENT C++ parser accepts (NodeConfig.cpp:1396-1408). Narrow this
# tuple (never widen the output beyond it) if the parser is older.
ACCEPTED_OP_FORK_KEYS = tuple(key for key, _ in OP_FORK_LADDER)


def die(msg):
    sys.stderr.write(f"error: {msg}\n")
    raise SystemExit(1)


try:
    import rlp
    from eth_hash.auto import keccak
    from trie import HexaryTrie
except ImportError as _error:  # pragma: no cover - dependency hint only
    die(f"{_error}: install tools/.ci/c2-e2e-requirements.txt (eth-hash, rlp, trie)")


def strip0x(value):
    text = str(value).strip().lower()
    return text[2:] if text.startswith("0x") else text


def parse_amount(value, label):
    """Parse a JSON balance/nonce/timestamp: int, 0x-hex or decimal string."""
    if value is None:
        return 0
    if isinstance(value, bool):
        die(f"{label}: boolean is not a quantity")
    if isinstance(value, int):
        return value
    text = str(value).strip().lower()
    if text.startswith("0x"):
        return int(text, 16) if len(text) > 2 else 0
    if text.isdigit():
        return int(text, 10)
    die(f"{label}: not a quantity: {value!r}")


def quantity_hex(value, label):
    """Render as a minimal 0x QUANTITY (requireHexField, expectedLen=0)."""
    number = parse_amount(value, label)
    if number < 0 or number > UINT256_MAX:
        die(f"{label}: quantity out of u256 range: {value!r}")
    return hex(number)


def fixed_hex(value, width, label):
    """Render as 0x-prefixed lowercase hex with exactly `width` hex chars."""
    if value is None:
        die(f"{label}: missing required value")
    body = strip0x(value)
    if len(body) > width:
        die(f"{label}: longer than {width} hex chars: {value!r}")
    try:
        int(body or "0", 16)
    except ValueError:
        die(f"{label}: not valid hex: {value!r}")
    return "0x" + body.rjust(width, "0")


def bytes_hex(value, label):
    """Variable-length 0x-prefixed even hex ("0x" allowed) — code/extra_data."""
    if value is None:
        return "0x"
    body = strip0x(value)
    try:
        int(body or "0", 16)
    except ValueError:
        die(f"{label}: not valid hex: {value!r}")
    if len(body) % 2:
        die(f"{label}: odd-length hex: {value!r}")
    return "0x" + body


def load_json(path):
    try:
        with open(path) as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        die(f"cannot read {path}: {error}")


# ---------------------------------------------------------------------------
# alloc
# ---------------------------------------------------------------------------

def load_allocs(genesis, source):
    """genesis alloc -> list of normalized accounts, ascending address order."""
    alloc = genesis.get("alloc")
    if not isinstance(alloc, dict):
        die(f"{source}: missing or invalid 'alloc' object")
    accounts = {}
    for raw_address, account in alloc.items():
        if not isinstance(account, dict):
            die(f"{source}: alloc[{raw_address!r}] is not an object")
        addr_hex = strip0x(raw_address)
        if len(addr_hex) > 40:
            die(f"{source}: alloc address longer than 20 bytes: {raw_address!r}")
        try:
            addr_int = int(addr_hex or "0", 16)
        except ValueError:
            die(f"{source}: alloc address is not hex: {raw_address!r}")
        if addr_int in accounts:
            die(f"{source}: duplicate alloc address 0x{addr_int:040x}")

        balance = parse_amount(account.get("balance", 0), f"alloc[{raw_address}].balance")
        if balance < 0 or balance > UINT256_MAX:
            die(f"{source}: alloc[{raw_address}].balance out of u256 range")
        nonce = parse_amount(account.get("nonce", 0), f"alloc[{raw_address}].nonce")
        if nonce < 0 or nonce > UINT64_MAX:
            die(f"{source}: alloc[{raw_address}].nonce does not fit uint64: {nonce}")

        code = bytes_hex(account.get("code"), f"alloc[{raw_address}].code")

        storage = {}
        for raw_slot, raw_value in (account.get("storage") or {}).items():
            slot = fixed_hex(raw_slot, 64, f"alloc[{raw_address}].storage key")
            value = fixed_hex(raw_value, 64, f"alloc[{raw_address}].storage value")
            storage[slot] = value
        accounts[addr_int] = {
            "address": f"0x{addr_int:040x}",
            "balance": balance,
            "nonce": nonce,
            "code": code,
            "storage": storage,
        }
    return [accounts[addr] for addr in sorted(accounts)]


def emit_allocs(allocs):
    lines = []
    for index, account in enumerate(allocs):
        lines.append(f"[alloc.{index}]")
        lines.append(f"    address={account['address']}")
        # balance/nonce are decimal: NodeConfig.cpp:303-307 requireDecimalField.
        lines.append(f"    balance={account['balance']}")
        lines.append(f"    nonce={account['nonce']}")
        # EOA = no code line at all (NodeConfig.cpp:324-331).
        if account["code"] != "0x":
            lines.append(f"    code={account['code']}")
        if account["storage"]:
            lines.append(f"[alloc.{index}.storage]")
            for slot in sorted(account["storage"]):
                lines.append(f"    {slot}={account['storage'][slot]}")
        lines.append("")
    return lines


def storage_root(storage):
    """Secure storage trie root; zero-valued slots do not exist in the trie."""
    trie = HexaryTrie(db={})
    for slot, value in storage.items():
        number = int(value, 16)
        if number:
            trie[keccak(bytes.fromhex(slot[2:]))] = rlp.encode(number)
    return trie.root_hash


def state_root(allocs):
    """Secure state trie root over the accounts, as op-geth Genesis.ToBlock builds it
    (and Ledger::computeGenesisStateTrie recomputes at boot)."""
    trie = HexaryTrie(db={})
    for account in allocs:
        code = bytes.fromhex(account["code"][2:])
        trie[keccak(bytes.fromhex(account["address"][2:]))] = rlp.encode([
            account["nonce"], account["balance"], storage_root(account["storage"]),
            keccak(code)])
    return trie.root_hash


# ---------------------------------------------------------------------------
# eth_genesis_header
# ---------------------------------------------------------------------------

def derive_fork_fields(genesis, timestamp, allocs):
    """The fork-gated header fields op-geth's Genesis.ToBlock (core/genesis.go
    toBlockWithRoot) sets for this genesis, keyed by the [eth_genesis_header] name, with
    op-geth's defaults; a key is absent when its fork is not active at genesis."""
    config = genesis.get("config") or {}

    def active(key, now):
        value = config.get(key)
        return value is not None and parse_amount(value, key) <= now

    derived = {}
    if active("londonBlock", 0):
        derived["base_fee_per_gas"] = hex(INITIAL_BASE_FEE)
    if active("shanghaiTime", timestamp):
        derived["withdrawals_root"] = EMPTY_TRIE_ROOT
    if active("cancunTime", timestamp):
        derived.update(blob_gas_used="0x0", excess_blob_gas="0x0",
                       parent_beacon_block_root=ZERO_HASH)
    if active("pragueTime", timestamp):
        derived["requests_hash"] = EMPTY_REQUESTS_HASH
    if active("isthmusTime", timestamp):
        # statedb.GetStorageRoot: the passer's storage root, the zero hash if it is absent.
        passer = [a for a in allocs if int(a["address"], 16) == L2_TO_L1_MESSAGE_PASSER]
        derived["withdrawals_root"] = (
            "0x" + storage_root(passer[0]["storage"]).hex() if passer else ZERO_HASH)
    if active("amsterdamTime", timestamp):
        die("Amsterdam active at genesis: op-geth adds header fields this tool does not emit")
    return derived


def header_hash(fields):
    """keccak256(rlp(header)) over every field but `hash`, in the geth Header order the
    fields list already has."""
    quantities = ("difficulty", "number", "gas_limit", "gas_used", "timestamp",
                  "base_fee_per_gas", "blob_gas_used", "excess_blob_gas")
    items = [int(value, 16) if key in quantities else bytes.fromhex(value[2:])
             for key, value in fields]
    return "0x" + keccak(rlp.encode(items)).hex()


def build_header(genesis, source, genesis_hash, allocs):
    """Return the ordered [(key, value)] header over `allocs`. A stateRoot in the genesis
    JSON and `genesis_hash` (when given) must match what these accounts produce."""

    def get(*names):
        for name in names:
            if name in genesis and genesis[name] is not None:
                return genesis[name]
        return None

    computed_root = "0x" + state_root(allocs).hex()
    if get("stateRoot") is not None and \
            fixed_hex(get("stateRoot"), 64, "stateRoot") != computed_root:
        die(f"{source}: stateRoot {get('stateRoot')} != computed {computed_root}")

    number = parse_amount(get("number") or 0, "number")
    if number != 0:
        die(f"{source}: genesis number must be 0 (NodeConfig.cpp:421-426 "
            f"hard-requires number=0x0), got {number}")

    timestamp = parse_amount(get("timestamp") or 0, "timestamp")
    if timestamp > MAX_TIMESTAMP_SEC:
        die(f"{source}: timestamp {timestamp} exceeds int64 milliseconds bound "
            f"(NodeConfig.cpp:430-438)")

    if not parse_amount(get("gasLimit") or 0, "gasLimit"):
        # op-geth would substitute params.GenesisGasLimit; an OP genesis always sets it.
        die(f"{source}: missing or zero gasLimit")

    fields = [
        ("parent_hash", fixed_hex(get("parentHash") or ZERO_HASH, 64, "parentHash")),
        ("sha3_uncles", fixed_hex(get("sha3Uncles") or EMPTY_UNCLES_HASH, 64, "sha3Uncles")),
        ("miner", fixed_hex(get("coinbase", "miner") or ZERO_ADDRESS, 40, "coinbase")),
        ("state_root", computed_root),
        ("transactions_root",
         fixed_hex(get("transactionsRoot") or EMPTY_TRIE_ROOT, 64, "transactionsRoot")),
        ("receipts_root",
         fixed_hex(get("receiptsRoot") or EMPTY_TRIE_ROOT, 64, "receiptsRoot")),
        ("logs_bloom", fixed_hex(get("logsBloom") or ZERO_BLOOM, 512, "logsBloom")),
        ("difficulty", quantity_hex(get("difficulty") or 0, "difficulty")),
        ("number", "0x0"),
        ("gas_limit", quantity_hex(get("gasLimit"), "gasLimit")),
        ("gas_used", quantity_hex(get("gasUsed") or 0, "gasUsed")),
        ("timestamp", quantity_hex(timestamp, "timestamp")),
        ("extra_data", bytes_hex(get("extraData"), "extraData")),
        ("mix_hash", fixed_hex(get("mixHash") or ZERO_HASH, 64, "mixHash")),
        ("nonce", "0x" + format(parse_amount(get("nonce") or 0, "nonce"), "016x")),
    ]
    # Fork-gated fields (NodeConfig.cpp:453-482): emitted only when the JSON
    # carries them — an absent key is what omits the RLP field on re-encode.
    optional = [
        ("base_fee_per_gas", get("baseFeePerGas"), quantity_hex),
        ("withdrawals_root", get("withdrawalsRoot"),
         lambda v, l: fixed_hex(v, 64, l)),
        ("blob_gas_used", get("blobGasUsed"), quantity_hex),
        ("excess_blob_gas", get("excessBlobGas"), quantity_hex),
        ("parent_beacon_block_root", get("parentBeaconBlockRoot"),
         lambda v, l: fixed_hex(v, 64, l)),
        ("requests_hash", get("requestsHash"), lambda v, l: fixed_hex(v, 64, l)),
    ]
    # op-geth emits a field iff its fork is active at genesis. The base fee and the blob gas
    # counters take the genesis JSON value when set (g.BaseFee / g.BlobGasUsed /
    # g.ExcessBlobGas); the roots are always derived, so a JSON value must agree.
    derived = derive_fork_fields(genesis, timestamp, allocs)
    json_wins = ("base_fee_per_gas", "blob_gas_used", "excess_blob_gas")
    for ini_key, value, render in optional:
        if ini_key not in derived:
            if value is not None:
                die(f"{source}: {ini_key} is set but its fork is not active at genesis")
            continue
        rendered = render(derived[ini_key], ini_key)
        if value is not None and ini_key in json_wins:
            rendered = render(value, ini_key)
        elif value is not None and render(value, ini_key) != rendered:
            die(f"{source}: {ini_key} {value} != derived {rendered}")
        fields.append((ini_key, rendered))
    computed_hash = header_hash(fields)
    if genesis_hash is not None and fixed_hex(genesis_hash, 64, "hash") != computed_hash:
        die(f"{source}: genesis hash {genesis_hash} != computed {computed_hash}; the "
            "output would not reproduce the input chain's genesis")
    fields.append(("hash", computed_hash))
    return fields


def merge_alloc(base, merged, source):
    """--alloc must be the genesis accounts unchanged, plus any extra accounts
    (build-allocs.py adds proxies and implementations). Returns whether it added any."""
    by_address = {account["address"]: account for account in merged}
    for account in base:
        if by_address.get(account["address"]) != account:
            die(f"{source}: account {account['address']} of --genesis is missing or changed")
    return len(by_address) > len(base)


# ---------------------------------------------------------------------------
# op_fork_timestamps
# ---------------------------------------------------------------------------

def collect_fork_times(genesis, rollup):
    """Return ({ini_key: seconds}, source_description, skipped_known_keys)."""
    config = genesis.get("config")
    genesis_times = {}
    if isinstance(config, dict):
        for ini_key, camel in OP_FORK_LADDER:
            value = config.get(camel)
            if value is not None:
                genesis_times[ini_key] = value
    if rollup is None:
        source = "genesis JSON config" if genesis_times else "none (no fork times found)"
        return genesis_times, source

    rollup_times = {}
    for ini_key, _ in OP_FORK_LADDER:
        value = rollup.get(ini_key)  # rollup.json uses snake_case
        if value is not None:
            rollup_times[ini_key] = value
    return rollup_times, "rollup.json"


def emit_op_fork_times(raw_times, source):
    """Split into emitted keys (parser-accepted) and comment-only keys."""
    times = {}
    for ini_key, value in raw_times.items():
        seconds = parse_amount(value, ini_key)
        if seconds < 0 or seconds > UINT64_MAX:
            die(f"{ini_key}: fork timestamp out of uint64 range: {value!r}")
        times[ini_key] = seconds

    emitted, skipped = {}, {}
    for ini_key, _ in OP_FORK_LADDER:
        if ini_key not in times:
            continue
        if ini_key in ACCEPTED_OP_FORK_KEYS:
            emitted[ini_key] = times[ini_key]
        else:
            skipped[ini_key] = times[ini_key]

    # Mirror the parser's invariants (loadOpForkTimestamps):
    # 1. scheduled (non-omitted) times must be non-decreasing in fork order
    #    (NodeConfig.cpp:1438-1461) — an omitted fork is skipped, not terminal.
    ladder = [emitted[key] for key, _ in OP_FORK_LADDER if key in emitted]
    if ladder != sorted(ladder):
        die("op fork times are not non-decreasing down the fork ladder: "
            + ", ".join(f"{k}={v}" for k, v in emitted.items()))
    # 2. a scheduled pre-Isthmus fork without isthmus_time is never consulted
    #    (Isthmus is the zero-start baseline) — the parser rejects it
    #    (NodeConfig.cpp:1462-1482), so fail here first.
    pre_isthmus = [key for key, _ in OP_FORK_LADDER[:8]]
    if "isthmus_time" not in emitted:
        scheduled_low = [key for key in pre_isthmus if key in emitted]
        if scheduled_low:
            die(f"[op_fork_timestamps].{scheduled_low[0]} requires isthmus_time: "
                "without it Isthmus is the zero-start baseline and the "
                "pre-Isthmus rungs are never consulted (NodeConfig.cpp:1462-1482)")

    lines = ["[op_fork_timestamps]"]
    lines.append(f"    ; fork times from: {source}")
    for ini_key, seconds in skipped.items():
        lines.append(
            f"    ; {ini_key}={seconds}  ; known but NOT accepted by the current "
            f"parser (NodeConfig.cpp:1414-1426) — omitted on purpose")
    if not emitted and not skipped:
        lines.append("    ; no fork times found in the inputs")
    for ini_key, seconds in emitted.items():
        lines.append(f"    {ini_key}={seconds}")
    lines.append("")
    if not emitted:
        sys.stderr.write(
            "warning: [op_fork_timestamps] carries no parser-accepted key; boost's "
            "INI reader drops keyless sections, so validateL2Invariants "
            "(NodeConfig.cpp:598-606) will reject this genesis on the OP lane\n")
    return lines, skipped


# ---------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Convert an op-geth genesis JSON into a FISCO-BCOS "
                    "config.genesis fragment ([alloc.*] + [eth_genesis_header] "
                    "+ [op_fork_timestamps]).")
    parser.add_argument("--genesis", required=True, help="op-geth genesis JSON path")
    parser.add_argument("--rollup",
                        help="op-node rollup.json (fork times + genesis.l2.hash); "
                             "overrides the genesis JSON config for fork times")
    parser.add_argument("--genesis-hash",
                        help="canonical genesis block hash (0x...); overrides "
                             "rollup.json genesis.l2.hash")
    parser.add_argument("--name", default=None,
                        help="chain name for the header comment (e.g. op-sepolia)")
    parser.add_argument("--timestamp", action="store_true",
                        help="embed the generation time in the header comment "
                             "(default off: identical inputs must produce "
                             "byte-identical output)")
    parser.add_argument("--alloc",
                        help="geth-style alloc JSON (address -> account) replacing the "
                             "genesis JSON's alloc, e.g. build-allocs.py --out-json")
    parser.add_argument("-o", "--output", required=True, help="output INI path")
    args = parser.parse_args(argv)

    genesis = load_json(args.genesis)
    if not isinstance(genesis, dict):
        die(f"{args.genesis}: top level must be a JSON object")
    rollup = load_json(args.rollup) if args.rollup else None

    chain_id = genesis.get("chainId")
    if chain_id is None and isinstance(genesis.get("config"), dict):
        chain_id = genesis["config"].get("chainId")
    if chain_id is None and rollup is not None:
        chain_id = rollup.get("l2_chain_id")
    if chain_id is None:
        sys.stderr.write("warning: chainId not found in genesis/rollup inputs\n")

    genesis_hash = args.genesis_hash
    if genesis_hash is None and rollup is not None:
        genesis_hash = ((rollup.get("genesis") or {}).get("l2") or {}).get("hash")
    l2_time = None
    if rollup is not None:
        l2_time = (rollup.get("genesis") or {}).get("l2_time")
        if l2_time is not None:
            json_ts = parse_amount(genesis.get("timestamp") or 0, "timestamp")
            if parse_amount(l2_time, "rollup genesis.l2_time") != json_ts:
                sys.stderr.write(
                    f"warning: rollup genesis.l2_time ({l2_time}) != genesis "
                    f"timestamp ({json_ts})\n")

    allocs = load_allocs(genesis, args.genesis)
    # The inputs (stateRoot, --genesis-hash / rollup hash) describe the --genesis chain:
    # checked against its own accounts even when --alloc adds accounts.
    header_fields = build_header(genesis, args.genesis, genesis_hash, allocs)
    if args.alloc:
        base = allocs
        allocs = load_allocs({"alloc": load_json(args.alloc)}, args.alloc)
        if merge_alloc(base, allocs, args.alloc):
            header_fields = build_header(genesis, args.genesis, None, allocs)
            if genesis_hash is not None:
                sys.stderr.write(f"warning: {genesis_hash} is the genesis hash WITHOUT the "
                                 "accounts --alloc adds; set rollup.json genesis.l2.hash to "
                                 f"{header_fields[-1][1]}\n")
    raw_times, fork_source = collect_fork_times(genesis, rollup)

    lines = []
    lines.append("; " + "=" * 75)
    lines.append("; FISCO-BCOS config.genesis fragment — converted from an op-geth "
                 "genesis JSON")
    lines.append(f"; chain: {args.name or 'unknown'}  chainId: "
                 f"{chain_id if chain_id is not None else 'unknown'}")
    lines.append(f"; source genesis: {args.genesis}")
    if args.rollup:
        lines.append(f"; fork times / genesis hash from rollup: {args.rollup}")
    if args.timestamp:
        lines.append("; generated: "
                     + datetime.datetime.now(datetime.timezone.utc).strftime(
                         "%Y-%m-%d %H:%M:%S UTC"))
    lines.append(";")
    if args.alloc:
        lines.append(f"; accounts from: {args.alloc}")
    lines.append("; Node startup (Ledger::buildGenesisBlock) recomputes state_root from")
    lines.append("; [alloc.*] and keccak(rlp(header)) from the header fields and refuses")
    lines.append("; to boot on mismatch; eth-sync-check --genesis-ini runs the same checks.")
    lines.append("; " + "=" * 75)
    lines.append("")
    lines.append(f"; {len(allocs)} genesis accounts, ascending address order; "
                 "balance/nonce decimal, code/storage 0x-hex.")
    lines.extend(emit_allocs(allocs))

    lines.append("[eth_genesis_header]")
    for key, value in header_fields:
        lines.append(f"    {key}={value}")
    lines.append("")

    fork_lines, skipped = emit_op_fork_times(raw_times, fork_source)
    lines.extend(fork_lines)

    output = "\n".join(lines) + "\n"
    Path(args.output).write_text(output)
    sys.stderr.write(
        f"wrote {len(allocs)} alloc section(s), [eth_genesis_header] "
        f"({len(header_fields)} keys), [op_fork_timestamps] -> {args.output}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
