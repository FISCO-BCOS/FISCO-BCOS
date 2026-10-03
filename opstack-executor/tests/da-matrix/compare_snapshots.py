#!/usr/bin/env python3
"""compare_snapshots.py — value-level four-source snapshot comparison.

Compares the committed golden snapshots (fisco / opgeth / oprevm / solidity)
per grid case on {id, l1_cost, operator_cost}, ignoring JSON byte layout
(jsoncpp puts a space before ':' in the FISCO file; Go/Rust do not).

Known divergence handling (mirrors the grid `known_divergence` field):
  - `karst_alias`       -> switch_karst: all ends are expected to AGREE (by
                           design), so it is still compared, not skipped.
  - Solidity `l1_cost`  -> NOT compared against the other ends (unsigned-tx +68
                           convention, see DIVERGENCES.md); only its
                           `operator_cost` is authoritative. Reported separately.

Exit code 0 when every compared field matches; 1 on any mismatch.

Usage: compare_snapshots.py [golden_dir]
"""

import json
import sys


def load(path):
    with open(path) as f:
        return {c["id"]: (c["l1_cost"], c["operator_cost"]) for c in json.load(f)}


def main() -> int:
    golden = sys.argv[1] if len(sys.argv) > 1 else "golden"
    ends = {
        "fisco": f"{golden}/fisco/out_fisco.json",
        "opgeth": f"{golden}/opgeth/out_opgeth.json",
        "oprevm": f"{golden}/oprevm/out_oprevm.json",
        "solidity": f"{golden}/solidity/out_solidity.json",
    }
    data = {}
    for name, path in ends.items():
        try:
            data[name] = load(path)
        except FileNotFoundError:
            print(f"[warn] {name}: snapshot missing ({path}) — skipped")
            data[name] = None

    have = {k for k, v in data.items() if v is not None}
    if len(have) < 2:
        print("error: need at least two snapshots to compare")
        return 2
    if not data["fisco"]:
        print("error: fisco snapshot is the comparison base and is missing")
        return 2

    base = data["fisco"]
    mismatches = 0
    compared = 0
    for cid in base:
        for name in have - {"fisco", "solidity"}:
            other = data[name]
            if cid not in other:
                print(f"MISSING {cid} in {name}")
                mismatches += 1
                continue
            compared += 1
            if base[cid] != other[cid]:
                print(f"MISMATCH {cid} {name}: fisco={base[cid]} {name}={other[cid]}")
                mismatches += 1
        if data.get("solidity") and cid in data["solidity"]:
            sol = data["solidity"][cid]
            # operator_cost is authoritative; l1_cost is cross-reference only.
            if sol[1] != base[cid][1]:
                print(f"MISMATCH {cid} solidity operator: fisco={base[cid][1]} sol={sol[1]}")
                mismatches += 1

    print(f"compared {compared} l1+op fields across fisco/opgeth/oprevm "
          f"(+ {len(base)} operator fields vs solidity)")
    print(f"mismatches: {mismatches}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
