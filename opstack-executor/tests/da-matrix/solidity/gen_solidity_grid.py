#!/usr/bin/env python3
"""gen_solidity_grid.py — regenerate the Solidity Case array from da_matrix.json.

Reads the da-matrix grid (the single source of truth) and prints a Solidity
struct-array initializer fragment for OperatorFeeCheck.t.sol:

    python3 gen_solidity_grid.py <da_matrix.json> > cases_fragment.sol

Assembly: the fragment is the comma-separated body of a `Case[]` literal. It is
spliced into `test/OperatorFeeCheck.t.sol`'s setUp() between

    cases = [
    ... fragment here ...
    ];

Keep the field order in sync with the `Case` struct in the test.
"""

import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gen_solidity_grid.py <da_matrix.json>", file=sys.stderr)
        return 2

    import json

    with open(sys.argv[1]) as f:
        grid = json.load(f)

    envelopes = grid["envelopes"]
    lines = []
    for c in grid["cases"]:
        slots = c["slots"]
        s1 = bytes.fromhex(slots["1"][2:])
        s3 = bytes.fromhex(slots["3"][2:])
        s7 = bytes.fromhex(slots["7"][2:])
        s8 = bytes.fromhex(slots["8"][2:])
        basefee = int.from_bytes(s1, "big")
        blob_base_fee = int.from_bytes(s7, "big")
        base_fee_scalar = int.from_bytes(s3[16:20], "big")
        blob_base_fee_scalar = int.from_bytes(s3[20:24], "big")
        op_scalar = int.from_bytes(s8[20:24], "big")
        op_const = int.from_bytes(s8[24:32], "big")
        da = int.from_bytes(s8[18:20], "big")
        env = envelopes[c["envelope_ref"]]
        lines.append(
            "        Case({"
            f'id: "{c["id"]}", '
            f'envelope: hex"{env[2:]}", '
            f'gas: {c["gas"]}, '
            f'fork: "{c["fork"]}", '
            f"basefee: {basefee}, "
            f"blobBaseFee: {blob_base_fee}, "
            f"baseFeeScalar: {base_fee_scalar}, "
            f"blobBaseFeeScalar: {blob_base_fee_scalar}, "
            f"operatorFeeScalar: {op_scalar}, "
            f"operatorFeeConstant: {op_const}, "
            f"daFootprintGasScalar: {da}"
            "}),"
        )
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
