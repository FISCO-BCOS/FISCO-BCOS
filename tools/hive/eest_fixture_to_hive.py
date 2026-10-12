#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""eest_fixture_to_hive.py — convert an EEST blockchain-test fixture into the
hive client input pair (genesis.json + chain.rlp), for host-level testing of
the hive client wrapper without docker.

Usage:
    python3 tools/hive/eest_fixture_to_hive.py <fixture.json> [--test NAME] \
        --out-genesis genesis.json --out-chain chain.rlp

The fixture's genesisBlockHeader.hash is printed at the end; after starting a
node on the converted data, eth_getBlockByNumber(0).hash must equal it.
"""
import argparse
import json
import sys

# Fixture network name -> timestamp forks active from genesis. The fixtures
# exercise a single fork (or a transition); only single-fork networks are
# supported here.
FORK_TIMES = {
    "Paris": {},  # post-merge, pre-Shanghai: no timestamp forks
    "Shanghai": {"shanghaiTime": 0},
    "Cancun": {"shanghaiTime": 0, "cancunTime": 0},
    "Prague": {"shanghaiTime": 0, "cancunTime": 0, "pragueTime": 0},
    "Osaka": {"shanghaiTime": 0, "cancunTime": 0, "pragueTime": 0, "osakaTime": 0},
    # Fork-transition networks (EEST "XToYAtTime15k"): Y activates at timestamp 15000.
    "ShanghaiToCancunAtTime15k": {"shanghaiTime": 0, "cancunTime": 15000},
    "CancunToPragueAtTime15k": {"shanghaiTime": 0, "cancunTime": 0, "pragueTime": 15000},
    "ParisToShanghaiAtTime15k": {"shanghaiTime": 15000},
    "PragueToOsakaAtTime15k": {"shanghaiTime": 0, "cancunTime": 0, "pragueTime": 0, "osakaTime": 15000},
}

HEADER_KEY_MAP = {  # fixture genesisBlockHeader key -> geth genesis.json key
    "coinbase": "coinbase",
    "difficulty": "difficulty",
    "extraData": "extraData",
    "gasLimit": "gasLimit",
    "gasUsed": "gasUsed",
    "mixHash": "mixHash",
    "nonce": "nonce",
    "parentHash": "parentHash",
    "timestamp": "timestamp",
    "baseFeePerGas": "baseFeePerGas",
    "blobGasUsed": "blobGasUsed",
    "excessBlobGas": "excessBlobGas",
    "withdrawalsRoot": "withdrawalsHash",  # NOTE: geth genesis.json spelling
    "parentBeaconBlockRoot": "parentBeaconBlockRoot",
    "requestsHash": "requestsHash",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixture")
    ap.add_argument("--test", help="test name inside the fixture file (default: first)")
    ap.add_argument(
        "--include-invalid",
        action="store_true",
        help="keep expectException blocks in the chain (matches the hive consume-rlp "
        "contract: the client must reject them; default strips them for plain "
        "import-to-head checks)",
    )
    ap.add_argument("--out-genesis", required=True)
    ap.add_argument("--out-chain", required=True)
    args = ap.parse_args()

    with open(args.fixture) as fh:
        doc = json.load(fh)
    name = args.test or next(iter(doc))
    test = doc[name]

    network = test["config"]["network"]
    # Transition networks ("CancunToPragueAtTime15k") are not single-fork; take
    # the leading fork's schedule only when it is a plain network name.
    if network not in FORK_TIMES:
        sys.exit(f"unsupported network {network!r} (need one of {sorted(FORK_TIMES)})")

    gh = test["genesisBlockHeader"]
    genesis = {
        "config": {
            "chainId": int(test["config"].get("chainid", "0x1"), 16),
            "homesteadBlock": 0,
            "eip150Block": 0,
            "eip155Block": 0,
            "eip158Block": 0,
            "byzantiumBlock": 0,
            "constantinopleBlock": 0,
            "petersburgBlock": 0,
            "istanbulBlock": 0,
            "berlinBlock": 0,
            "londonBlock": 0,
            "terminalTotalDifficulty": 0,
            **FORK_TIMES[network],
        },
        "alloc": test["pre"],
    }
    for fk, gk in HEADER_KEY_MAP.items():
        if fk in gh:
            genesis[gk] = gh[fk]
    genesis.setdefault("extraData", "0x")
    genesis.setdefault("nonce", "0x0000000000000000")

    with open(args.out_genesis, "w") as fh:
        json.dump(genesis, fh, indent=1)

    n = 0
    with open(args.out_chain, "wb") as out:
        # Blocks without a blocknumber (typical for expectException entries) sort
        # last, preserving the fixture's tail position: consume-rlp streams them
        # in fixture order, so the invalid block is offered after its parent.
        for block in sorted(
            test["blocks"], key=lambda b: int(b.get("blocknumber") or "0xffffffffffff", 16)
        ):
            if "expectException" in block and not args.include_invalid:
                print(f"skipping invalid block {block.get('blocknumber')} (expectException)")
                continue
            out.write(bytes.fromhex(block["rlp"][2:]))
            n += 1

    print(f"fixture:  {name} (network {network})")
    print(f"genesis:  {args.out_genesis} (alloc={len(test['pre'])})")
    print(f"chain:    {args.out_chain} ({n} block(s))")
    print(f"expect genesis hash: {gh['hash']}")
    print(f"expect head:         {test['lastblockhash']}")


if __name__ == "__main__":
    main()
