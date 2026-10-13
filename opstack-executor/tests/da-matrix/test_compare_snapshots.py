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
"""Probes for compare_snapshots.py's field-scoped comparison (review F21).

The committed goldens all agree, so the comparator's mismatch path — and in
particular the operator_cost comparison that survives a tagged row (review F23:
a known_divergence exempts only l1_cost, and end-scoped per review F18) — had no
committed probe: a comparator that stopped comparing operator_cost, or that
started exempting it behind a tag, would still exit 0 on the goldens.

Each test copies golden/ to a tmp dir (never touching the committed snapshots),
optionally flips ONE field of the switch_karst row in the copied FISCO end, runs
compare_snapshots.py and asserts the exit code / mismatch line.

Run: cd opstack-executor/tests/da-matrix && python3 -m pytest test_compare_snapshots.py -q
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPARATOR = HERE / "compare_snapshots.py"
GRID = HERE / "da_matrix.json"
GOLDEN = HERE / "golden"

# A tagged row: its l1_cost is still compared against opgeth/oprevm (karst_alias
# is COMPARED, not skipped), so flipping either field must fail the run.
TAGGED_ROW = "switch_karst"


def copy_golden(tmp_path):
    """Copy the committed golden tree; the comparator runs against the copy."""
    dest = tmp_path / "golden"
    shutil.copytree(GOLDEN, dest)
    return dest


def run_compare(golden_dir):
    return subprocess.run(
        [sys.executable, str(COMPARATOR), str(golden_dir), str(GRID)],
        capture_output=True, text=True)


def flip_field(golden_dir, end, case_id, field, value):
    """Replace one case's field value in the copied <end> snapshot."""
    path = golden_dir / end / f"out_{end}.json"
    rows = json.loads(path.read_text())
    for row in rows:
        if row["id"] == case_id:
            row[field] = value
            break
    else:
        raise AssertionError(f"case {case_id!r} not found in {path}")
    path.write_text(json.dumps(rows))


def test_pristine_goldens_compare_clean(tmp_path):
    proc = run_compare(copy_golden(tmp_path))
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "mismatches: 0" in proc.stdout


def test_operator_cost_flip_fails_on_a_tagged_row(tmp_path):
    golden = copy_golden(tmp_path)
    flip_field(golden, "fisco", TAGGED_ROW, "operator_cost", "0xdeadbeef")
    proc = run_compare(golden)
    assert proc.returncode == 1, proc.stdout + proc.stderr
    # The tag must not exempt the operator_cost comparison on ANY end: assert the
    # opgeth/oprevm lines, not just "a mismatch line" (the solidity end alone
    # would satisfy the weaker assertion even if the other two stopped comparing).
    assert f"MISMATCH {TAGGED_ROW} opgeth" in proc.stdout, proc.stdout
    assert f"MISMATCH {TAGGED_ROW} oprevm" in proc.stdout, proc.stdout


def test_l1_cost_flip_fails_on_a_tagged_row(tmp_path):
    # karst_alias is COMPARED (not in SKIPPED_DIVERGENCES): the row's l1_cost is
    # still compared against opgeth/oprevm, so a drift there must fail.
    golden = copy_golden(tmp_path)
    flip_field(golden, "fisco", TAGGED_ROW, "l1_cost", "0xdeadbeef")
    proc = run_compare(golden)
    assert proc.returncode == 1, proc.stdout + proc.stderr
    assert f"MISMATCH {TAGGED_ROW} opgeth" in proc.stdout, proc.stdout
    assert f"MISMATCH {TAGGED_ROW} oprevm" in proc.stdout, proc.stdout
