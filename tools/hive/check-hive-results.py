#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# check-hive-results.py — assert on a hive run's per-test results.
#
# The hive binary exits 0 even when tests fail (the failure count only goes to
# the log), so CI gates must parse the run JSON it writes under
# workspace/logs/. This script picks the newest run for a simulator and checks
# it: every test must pass, except an explicit allow-list of known-failure
# names which must fail EXACTLY (no more, no fewer — a fixed known failure
# turning green also fails the check, so the allow-list cannot rot).
#
# Usage:
#   check-hive-results.py --logs hive/workspace/logs --sim smoke/genesis \
#       --expect-total 6 --allow-fail "empty genesis" --allow-fail "all forks"
import argparse
import glob
import json
import os
import sys


def newest_run(logs_dir, sim_suffix):
    candidates = []
    for path in glob.glob(os.path.join(logs_dir, "*-*.json")):
        try:
            with open(path) as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name") or ""
        # Suite names are the simulator's basename ("genesis" for
        # "smoke/genesis"); match on the suffix so both spellings work.
        if name == sim_suffix or name == sim_suffix.split("/")[-1]:
            candidates.append((os.path.getmtime(path), path, data))
    if not candidates:
        return None, None
    candidates.sort()
    return candidates[-1][1], candidates[-1][2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--logs", required=True, help="hive workspace/logs directory")
    parser.add_argument("--sim", required=True, help="simulator name, e.g. smoke/genesis")
    parser.add_argument("--expect-total", type=int, default=None)
    parser.add_argument("--allow-fail", action="append", default=[],
                        help="test name expected to fail (repeatable)")
    args = parser.parse_args()

    path, data = newest_run(args.logs, args.sim)
    if data is None:
        print(f"ERROR: no run for simulator {args.sim} under {args.logs}")
        return 1
    test_cases = data.get("testCases", {})
    total = len(test_cases)
    failures = sorted(
        v.get("name", "?") for v in test_cases.values()
        if not v.get("summaryResult", {}).get("pass")
    )
    print(f"run {os.path.basename(path)}: {total} tests, {len(failures)} failed")
    for name in failures:
        print(f"  FAIL: {name}")

    ok = True
    if args.expect_total is not None and total != args.expect_total:
        print(f"ERROR: expected {args.expect_total} tests, got {total}")
        ok = False
    if sorted(args.allow_fail) != failures:
        print("ERROR: failure set differs from the allow-list")
        print(f"  expected failures: {sorted(args.allow_fail)}")
        print(f"  actual failures:   {failures}")
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
