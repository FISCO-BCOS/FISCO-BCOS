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

state_root is NOT part of a geth genesis JSON (it is derived from the allocs).
This tool does not compute the MPT; when the JSON carries no stateRoot it
emits a zero PLACEHOLDER. Ledger::buildGenesisBlock recomputes the state root
from [alloc.*] and refuses to boot on mismatch, so a placeholder fails safe —
fill it in from `eth-sync-check --genesis <genesis.json>` (or the chain's
published value) and verify the whole fragment with
`eth-sync-check --genesis-ini <config.genesis>` before starting a node.

Usage:
    python3 tools/op-genesis-to-allocs.py --genesis genesis.json \\
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

    # then fill state_root and verify:
    eth-sync-check --genesis op-genesis.json            # prints the state root
    eth-sync-check --genesis-ini <merged config.genesis> # re-parses + re-roots
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


# ---------------------------------------------------------------------------
# eth_genesis_header
# ---------------------------------------------------------------------------

def build_header(genesis, source, genesis_hash):
    """Return (ordered [(key, value)], placeholder_warnings)."""
    warnings = []

    def get(*names):
        for name in names:
            if name in genesis and genesis[name] is not None:
                return genesis[name]
        return None

    state_root = get("stateRoot")
    if state_root is None:
        state_root = ZERO_HASH
        warnings.append(
            "state_root: not present in the genesis JSON — PLACEHOLDER zeros "
            "emitted; fill from `eth-sync-check --genesis` output before boot")
    if genesis_hash is None:
        genesis_hash = ZERO_HASH
        warnings.append(
            "hash: no genesis hash available (pass --genesis-hash or a "
            "rollup.json carrying genesis.l2.hash) — PLACEHOLDER zeros emitted")

    number = parse_amount(get("number") or 0, "number")
    if number != 0:
        die(f"{source}: genesis number must be 0 (NodeConfig.cpp:421-426 "
            f"hard-requires number=0x0), got {number}")

    timestamp = parse_amount(get("timestamp") or 0, "timestamp")
    if timestamp > MAX_TIMESTAMP_SEC:
        die(f"{source}: timestamp {timestamp} exceeds int64 milliseconds bound "
            f"(NodeConfig.cpp:430-438)")

    if get("gasLimit") is None:
        die(f"{source}: missing gasLimit (required for [eth_genesis_header].gas_limit)")

    fields = [
        ("parent_hash", fixed_hex(get("parentHash") or ZERO_HASH, 64, "parentHash")),
        ("sha3_uncles", fixed_hex(get("sha3Uncles") or EMPTY_UNCLES_HASH, 64, "sha3Uncles")),
        ("miner", fixed_hex(get("coinbase", "miner") or ZERO_ADDRESS, 40, "coinbase")),
        ("state_root", fixed_hex(state_root, 64, "stateRoot")),
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
    for ini_key, value, render in optional:
        if value is not None:
            fields.append((ini_key, render(value, ini_key)))
    fields.append(("hash", fixed_hex(genesis_hash, 64, "hash")))
    return fields, warnings


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
    header_fields, warnings = build_header(genesis, args.genesis, genesis_hash)
    raw_times, fork_source = collect_fork_times(genesis, rollup)

    for warning in warnings:
        sys.stderr.write(f"warning: {warning}\n")

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
    lines.append("; VERIFY BEFORE BOOT:")
    lines.append(";   1. state_root (and hash, unless taken from the rollup) is a")
    lines.append(";      zero PLACEHOLDER when absent from the inputs. Fill")
    lines.append(";      state_root from:  eth-sync-check --genesis <genesis.json>")
    lines.append(";   2. Verify the merged genesis end-to-end:")
    lines.append(";        eth-sync-check --genesis-ini <config.genesis>")
    lines.append(";      Node startup (Ledger::buildGenesisBlock) recomputes the MPT")
    lines.append(";      stateRoot from [alloc.*] and keccak(rlp(header)) from the 21")
    lines.append(";      header fields, and refuses to boot on mismatch — a placeholder")
    lines.append(";      therefore fails safe.")
    lines.append("; " + "=" * 75)
    lines.append("")
    lines.append(f"; {len(allocs)} genesis accounts, ascending address order; "
                 "balance/nonce decimal, code/storage 0x-hex.")
    lines.extend(emit_allocs(allocs))

    lines.append("[eth_genesis_header]")
    for key, value in header_fields:
        if key == "state_root" and value == ZERO_HASH:
            lines.append("    ; FIXME: placeholder — replace with the state root "
                         "computed over the allocs above")
        if key == "hash" and value == ZERO_HASH:
            lines.append("    ; FIXME: placeholder — replace with the canonical "
                         "genesis block hash")
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
