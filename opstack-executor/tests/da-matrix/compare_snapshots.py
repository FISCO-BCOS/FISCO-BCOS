#!/usr/bin/env python3
# Copyright (c) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
# http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# @file compare_snapshots.py
"""compare_snapshots.py — value-level four-source snapshot comparison.

Compares the committed golden snapshots (fisco / opgeth / oprevm / solidity)
per grid case on {id, l1_cost, operator_cost}, ignoring JSON byte layout
(jsoncpp puts a space before ':' in the FISCO file; Go/Rust do not).

Every end must be present AND non-empty (a regeneration that writes `[]` is a
broken end, not a shrunk comparison), every id must be unique within an end,
and — when the grid is supplied — the snapshot id set must equal the grid's id
set: an extra snapshot row is a stale entry that would outlive the grid, and a
missing one is a hole. Both fail.

Known divergences (the grid's `known_divergence` field, registry =
DIVERGENCES.md): tags that mark a genuine cross-end difference —
l1_fee_saturation, flz_zero_clamp, solidity_l1_uint32_overflow,
da_footprint_overflow, da_footprint_176b_activation_shape — exempt ONLY the
l1_cost field of their case (their operator_cost is unaffected and stays
compared on every end) and are counted (run_fisco still computes and compares
the row against the fisco golden). `karst_alias` is registered too, but its ends
are expected to AGREE by construction, so it stays compared: the agreement is
the assertion. An unknown tag is fatal — a typo must not silently exempt a case.

Solidity `l1_cost` is NOT compared against the other ends (unsigned-tx +68
convention, see DIVERGENCES.md); only its `operator_cost` is authoritative.

Exit code 0 when every compared field matches; 1 on any mismatch; 2 on a
malformed input.

Usage: compare_snapshots.py <golden_dir> <da_matrix.json>
(Both arguments are required: without the grid the comparator cannot reconcile
the id sets, and the grid itself is the ONLY anchor for which cases must exist —
without an independent pin, a regeneration that drops a case from BOTH the grid
and the snapshots would silently shrink the gate.)
"""

import json
import sys

# The expected grid id set, pinned INDEPENDENTLY of da_matrix.json (review F11):
# the grid was previously the only authority, so deleting a case from the grid
# and regenerating all five snapshots kept both ctests green at reduced
# coverage. A regeneration that legitimately changes the case set must update
# this pin in the same change (the failure message says exactly that).
EXPECTED_GRID_IDS = frozenset({
    "baseline_isthmus",
    "baseline_jovian",
    "max_isthmus_scalars",
    "max_jovian_scalars",
    "overflow_isthmus",
    "overflow_jovian",
    "pre_isthmus_fjord",
    "pre_isthmus_granite",
    "pre_isthmus_holocene",
    "missing_isthmus_slot8_zero",
    "switch_ecotone",
    "switch_fjord",
    "switch_isthmus",
    "switch_jovian",
    "switch_karst",
    "blob_jovian_blob_scalar",
})

# The registry in DIVERGENCES.md. Keep in sync with that file. The two
# da_footprint_* tags are registered-but-never-triggered rows the registry tells
# future grid cases to carry — they must be known here, or following the
# registry aborts the gate with an unknown-tag error.
SKIPPED_DIVERGENCES = {
    "l1_fee_saturation",
    "flz_zero_clamp",
    "solidity_l1_uint32_overflow",
    "da_footprint_overflow",
    "da_footprint_176b_activation_shape",
}
# Registered but compared: the ends agree by construction, and the agreement is
# the (weak but free) assertion the row carries.
COMPARED_DIVERGENCES = {"karst_alias"}
KNOWN_DIVERGENCES = SKIPPED_DIVERGENCES | COMPARED_DIVERGENCES


def load(path, end):
    """Load one end's snapshot: {id: (l1_cost, operator_cost)}.

    Duplicate ids are fatal: the dict would keep the last row and silently
    compare a case set that is smaller than the file's.
    """
    try:
        with open(path) as f:
            rows = json.load(f)
    except json.JSONDecodeError as exc:
        # Malformed input is exit code 2 (the documented contract), not a traceback
        # with exit 1 — consumers distinguish input errors from mismatches.
        print(f"[fatal] {end}: malformed JSON ({path}): {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
    out = {}
    for row in rows:
        cid = row["id"]
        if cid in out:
            print(f"[fatal] {end}: duplicate case id {cid!r} in {path}", file=sys.stderr)
            raise SystemExit(2)
        out[cid] = (row["l1_cost"], row["operator_cost"])
    return out


def load_grid(path):
    """Grid case ids and the registered known-divergence rows."""
    with open(path) as f:
        grid = json.load(f)
    ids = set()
    registered = {}
    for case in grid["cases"]:
        cid = case["id"]
        if cid in ids:
            raise SystemExit(f"[fatal] grid: duplicate case id {cid!r} in {path}")
        ids.add(cid)
        tag = case.get("known_divergence")
        if tag is not None:
            if tag not in KNOWN_DIVERGENCES:
                raise SystemExit(
                    f"[fatal] grid: case {cid!r} carries unknown known_divergence "
                    f"{tag!r} — register it in DIVERGENCES.md and KNOWN_DIVERGENCES first")
            registered[cid] = tag
    return ids, registered


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <golden_dir> <da_matrix.json>\n"
              "(the grid argument is required — see the module docstring)",
              file=sys.stderr)
        return 2
    golden = sys.argv[1]
    grid_path = sys.argv[2]
    ends = {
        "fisco": f"{golden}/fisco/out_fisco.json",
        "opgeth": f"{golden}/opgeth/out_opgeth.json",
        "oprevm": f"{golden}/oprevm/out_oprevm.json",
        "solidity": f"{golden}/solidity/out_solidity.json",
    }
    data = {}
    for name, path in ends.items():
        try:
            data[name] = load(path, name)
        except FileNotFoundError as exc:
            # A missing snapshot must fail the comparison, not shrink the checked
            # set: deleting out_opgeth.json used to exit 0 while "comparing" two
            # ends only (fail-open, see F14). Exit 2: a missing input is a malformed
            # run, not a comparison mismatch (the documented exit-code contract).
            print(f"[fatal] {name}: snapshot missing ({path}) — the differential "
                  "gate requires every committed end", file=sys.stderr)
            raise SystemExit(2) from exc

    # Every end is load-bearing now (the operator-fee authority lives in the
    # solidity end): an emptied file is a broken end, not a shrunk comparison.
    for name, rows in data.items():
        if not rows:
            print(f"[fatal] {name}: snapshot is empty ({ends[name]}) — a committed "
                  "end must carry the grid's cases")
            return 2

    base = data["fisco"]
    registered = {}
    # The grid's own id set is pinned INDEPENDENTLY (EXPECTED_GRID_IDS, review
    # F11): a case deleted from the grid and from every snapshot would otherwise
    # pass every reconciliation below. A legitimate case-set change must update
    # the pin in the same commit.
    grid_ids, registered = load_grid(grid_path)
    if grid_ids != EXPECTED_GRID_IDS:
        dropped = sorted(EXPECTED_GRID_IDS - grid_ids)
        added = sorted(grid_ids - EXPECTED_GRID_IDS)
        print(f"[fatal] grid id set changed ({grid_path}): "
              f"dropped={dropped} added={added} — update EXPECTED_GRID_IDS in the "
              "same change as the regeneration, or the differential silently "
              "shrinks", file=sys.stderr)
        return 1
    for name, rows in data.items():
        extra = sorted(set(rows) - grid_ids)
        missing = sorted(grid_ids - set(rows))
        for cid in extra:
            print(f"STALE {cid} in {name}: not in the grid ({grid_path})")
        for cid in missing:
            print(f"MISSING {cid} from {name}: in the grid, absent from the snapshot")
        if extra or missing:
            print(f"mismatches: {len(extra) + len(missing)} case-set drift in {name}")
            return 1

    mismatches = 0
    compared = 0
    skipped = 0
    for cid in base:
        tag = registered.get(cid)
        # review F23: every registered divergence is field-scoped — an l1_cost
        # difference whose operator_cost is UNAFFECTED (DIVERGENCES.md: "getOperatorFee
        # is unaffected"). A tag therefore exempts only the l1_cost comparison of its
        # case; operator_cost stays compared on every end, so a tagged row can no
        # longer hide an operator-fee regression behind its tag.
        exempt_l1_cost = tag in SKIPPED_DIVERGENCES
        if exempt_l1_cost:
            print(f"SKIP-FIELD {cid} l1_cost known_divergence={tag} "
                  "(operator_cost still compared)")
            skipped += 1
        if tag in COMPARED_DIVERGENCES:
            print(f"NOTE {cid} known_divergence={tag}: still compared (ends agree by design)")
        for name in ("opgeth", "oprevm", "solidity"):
            other = data[name]
            if cid not in other:
                print(f"MISSING {cid} in {name}")
                mismatches += 1
                continue
            if name == "solidity":
                # operator_cost is authoritative; l1_cost is cross-reference only.
                compared += 1
                if other[cid][1] != base[cid][1]:
                    print(f"MISMATCH {cid} solidity operator: fisco={base[cid][1]} "
                          f"sol={other[cid][1]}")
                    mismatches += 1
            elif exempt_l1_cost:
                compared += 1
                if other[cid][1] != base[cid][1]:
                    print(f"MISMATCH {cid} {name} operator (l1_cost exempt "
                          f"known_divergence={tag}): fisco={base[cid][1]} "
                          f"{name}={other[cid][1]}")
                    mismatches += 1
            else:
                compared += 1
                if base[cid] != other[cid]:
                    print(f"MISMATCH {cid} {name}: fisco={base[cid]} {name}={other[cid]}")
                    mismatches += 1

    # Symmetry: a case present in another end but absent from the fisco base
    # would never be reached by the loop above.
    base_ids = set(base)
    for name, rows in data.items():
        if name == "fisco":
            continue
        for cid in sorted(set(rows) - base_ids):
            print(f"EXTRA {cid} in {name}: not in the fisco base")
            mismatches += 1

    print(f"compared {compared} fields across fisco/opgeth/oprevm/solidity")
    skipped_ids = sorted(cid for cid, tag in registered.items() if tag in SKIPPED_DIVERGENCES)
    if skipped_ids:
        print(f"skipped {len(skipped_ids)} registered known_divergence case(s): "
              + ", ".join(f"{cid} ({registered[cid]})" for cid in skipped_ids))
    print(f"mismatches: {mismatches}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
