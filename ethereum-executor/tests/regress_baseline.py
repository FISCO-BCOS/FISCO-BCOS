#!/usr/bin/env python3
"""Regression gate for the EEST baseline (Task 7).

Runs `eest-runner` over a configurable target, collects the structured
--json-failures output, and diffs the 5-tuple set
    (testName, forkName, dataIndex, gasIndex, valueIndex)
against the committed baseline (`bcos-evm/test/eth-eest-test/assets/
baseline-fails-v540.json`).  Any NEW failure — a 5-tuple that appears in the
new run but not in the baseline — exits non-zero (the regression trigger).

Targets:
  (default)   the PR smoke subset materialised by ethereum-executor/tests/
              CMakeLists.txt into <build>/eest-pr-smoke (deterministic,
              ~12 representative PASSING fixtures across forks/features).
  --fixture-dir DIR   an explicit fixture directory (recursive).
  --fixture FILE ...  one or more individual fixture files (run sequentially,
                      JSON merged).
  --full              the whole state_tests suite (nightly full run).

Exit codes:
  0   no new failures vs baseline (regression-free)
  1   >=1 new failure(s) (regression)
  2   infrastructure/usage error (missing runner, missing output, ...)

Self-test (rev.5): `python3 regress_baseline.py --self-test` feeds synthetic
baseline + synthetic new-failure records through the real diff/exit-code path
and asserts the non-zero / zero outcomes.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

BASELINE_REL = "bcos-evm/test/eth-eest-test/assets/baseline-fails-v540.json"
SMOKE_MANIFEST_REL = "manifest.txt"  # relative to the smoke dir


def repo_root() -> pathlib.Path:
    """Repo root = parents[2] of this file (tests/ -> ethereum-executor/ -> root)."""
    return pathlib.Path(__file__).resolve().parents[2]


def read_cmake_cache(cache_path: pathlib.Path) -> dict:
    """Parse the CMakeCache.txt key:type=value lines into a plain dict."""
    result = {}
    if not cache_path.is_file():
        return result
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key_part, _, value = line.partition("=")
        key = key_part.split(":", 1)[0]
        result[key] = value
    return result


def load_json(path) -> list:
    """Load a JSON array, tolerating a missing file (warns, returns [])."""
    p = pathlib.Path(path)
    if not p.is_file():
        print(f"[regress_baseline] WARNING: {p} not found; treating as empty list",
              file=sys.stderr)
        return []
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        print(f"[regress_baseline] WARNING: {p} is not valid JSON ({e}); treating as empty",
              file=sys.stderr)
        return []
    return data if isinstance(data, list) else []


def write_json(path, records: list) -> None:
    pathlib.Path(path).write_text(
        json.dumps(records, indent=2, sort_keys=True), encoding="utf-8")


def five_tuple(record: dict):
    """The 5-tuple that identifies a failure for regression-diff purposes."""
    return (record.get("testName", ""),
            record.get("forkName", ""),
            int(record.get("dataIndex", 0)),
            int(record.get("gasIndex", 0)),
            int(record.get("valueIndex", 0)))


def compute_new_failures(new_records: list, baseline_records: list) -> list:
    """Records present in the new run whose 5-tuple is NOT in the baseline.

    Order follows the new-run records (deterministic).  reason/category are
    deliberately ignored: a changed reason for the same test/fork/index combo
    is the same failure, not a new one.
    """
    baseline_tuples = {five_tuple(r) for r in baseline_records}
    return [r for r in new_records if five_tuple(r) not in baseline_tuples]


def exit_code_for(new_failures: list) -> int:
    """Map the diff result to the gate exit code (1 = regression)."""
    return 1 if new_failures else 0


def read_smoke_manifest(manifest_path) -> list:
    """Read the smoke-subset manifest (key=value lines) and return the FIXTURE paths."""
    p = pathlib.Path(manifest_path)
    if not p.is_file():
        return []
    return [line.split("=", 1)[1] for line in p.read_text(encoding="utf-8").splitlines()
            if line.startswith("FIXTURE=")]


def run_runner(runner: str, runner_args: list, json_out: pathlib.Path, env: dict):
    """Run eest-runner once; stream its output.  Returns returncode.

    A returncode in (0, 1) is expected (0 = all pass, 1 = some fixture failed);
    anything else means the runner itself crashed (infrastructure error).
    """
    cmd = [runner] + runner_args + ["--quiet", "--json-failures", str(json_out)]
    print(f"[regress_baseline] $ {' '.join(cmd)}")
    return subprocess.run(cmd, env=env).returncode


def run_multiple_fixtures(
        runner: str, fixture_files: list, json_out: pathlib.Path, env: dict) -> bool:
    """Run one file per eest-runner process (runner accepts ONE --fixture) and merge JSON."""
    merged: list = []
    for i, f in enumerate(fixture_files):
        tmp = json_out.with_name(f"{json_out.name}.{i}.json")
        tmp.unlink(missing_ok=True)
        rc = run_runner(runner, ["--fixture", f], tmp, env)
        if rc not in (0, 1):
            print(f"[regress_baseline] ERROR: eest-runner failed on {f} (returncode={rc})",
                  file=sys.stderr)
            return False
        if not tmp.is_file() or not tmp.read_text(encoding="utf-8",
                                                  errors="replace").strip():
            print(f"[regress_baseline] ERROR: eest-runner produced no --json-failures output "
                  f"for fixture {f}", file=sys.stderr)
            return False
        merged.extend(load_json(tmp))
        tmp.unlink()
    write_json(json_out, merged)
    return True


def print_report(new_failures: list, baseline_count: int, run_failure_count: int,
                 baseline_path: str) -> None:
    if new_failures:
        print(f"[regress_baseline] {len(new_failures)} NEW failure(s) vs "
              f"baseline {baseline_path} ({baseline_count} baseline failures):")
        for r in new_failures:
            print(f"  - {r.get('testName', '?')} [{r.get('forkName', '?')}] "
                  f"data={r.get('dataIndex', 0)} gas={r.get('gasIndex', 0)} "
                  f"value={r.get('valueIndex', 0)} "
                  f"category={r.get('category', '')} reason={r.get('reason', '')}")
        print(f"[regress_baseline] total new-run failures: {run_failure_count}")
    else:
        print(f"[regress_baseline] OK: {baseline_count} baseline failure(s), "
              f"0 new failures (total new-run failures: {run_failure_count}).")


def self_test() -> int:
    """Synthetic gate self-test: new failure -> non-zero, no-new-failure -> zero.

    Exercises the real compute_new_failures + exit_code_for path over real
    JSON files (temp dir), including the 5-tuple semantics (reason/category
    changes are NOT new failures).
    """
    ok = True
    with tempfile.TemporaryDirectory(prefix="regress_baseline_selftest_") as td:
        td = pathlib.Path(td)
        known = {"testName": "known/fail/A", "forkName": "Cancun",
                 "reason": "state mismatch", "category": "storage",
                 "dataIndex": 0, "gasIndex": 0, "valueIndex": 0}
        brand_new = {"testName": "new/fail/B", "forkName": "Prague",
                     "reason": "unexpected failure", "category": "nonce",
                     "dataIndex": 1, "gasIndex": 2, "valueIndex": 3}
        baseline_path = td / "baseline.json"
        write_json(baseline_path, [known])

        # Case 1: baseline + one brand-new failure -> exit 1.
        new_run = baseline_path.with_name("fails1.json")
        write_json(new_run, [known, brand_new])
        rc = exit_code_for(
            compute_new_failures(load_json(new_run), load_json(baseline_path)))
        status = "PASS" if rc == 1 else "FAIL"
        if rc != 1:
            ok = False
        print(f"self-test {status}: new failure -> exit {rc} (expected 1)")

        # Case 2: new run identical to baseline (no new failures) -> exit 0.
        same_run = baseline_path.with_name("fails2.json")
        write_json(same_run, [known])
        rc = exit_code_for(
            compute_new_failures(load_json(same_run), load_json(baseline_path)))
        status = "PASS" if rc == 0 else "FAIL"
        if rc != 0:
            ok = False
        print(f"self-test {status}: no new failure -> exit {rc} (expected 0)")

        # Case 3: empty new run -> exit 0.
        empty_run = baseline_path.with_name("fails3.json")
        write_json(empty_run, [])
        rc = exit_code_for(
            compute_new_failures(load_json(empty_run), load_json(baseline_path)))
        status = "PASS" if rc == 0 else "FAIL"
        if rc != 0:
            ok = False
        print(f"self-test {status}: empty run -> exit {rc} (expected 0)")

        # Case 4: same 5-tuple, different reason/category -> NOT a new failure.
        alt_run = baseline_path.with_name("fails4.json")
        write_json(alt_run, [dict(known, reason="changed reason", category="balance")])
        rc = exit_code_for(
            compute_new_failures(load_json(alt_run), load_json(baseline_path)))
        status = "PASS" if rc == 0 else "FAIL"
        if rc != 0:
            ok = False
        print(f"self-test {status}: 5-tuple diff ignores reason/category -> exit {rc} "
              "(expected 0)")

    print("self-test:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(
        prog="regress_baseline.py",
        description="Run eest-runner and diff --json-failures against the committed EEST "
                    "baseline; exit non-zero on NEW failures (regression gate).")
    parser.add_argument("--runner", default=None,
                        help="path to eest-runner (default: <build>/ethereum-executor/tests/"
                             "eest-runner, or $EEST_RUNNER)")
    parser.add_argument("--fixture-dir", default=None,
                        help="explicit fixture directory to run (recursive)")
    parser.add_argument("--fixture", action="append", default=None,
                        help="individual fixture file(s) to run (repeatable; merged)")
    parser.add_argument("--full", action="store_true",
                        help="run the whole state_tests suite (nightly full run)")
    parser.add_argument("--baseline", default=None,
                        help="baseline failures JSON (default: repo "
                             "bcos-evm/test/eth-eest-test/assets/baseline-fails-v540.json)")
    parser.add_argument("--json-failures", default=None,
                        help="where to write the new-run --json-failures (default: temp file)")
    parser.add_argument("--smoke-dir", default=None,
                        help="smoke subset dir (default: <build>/eest-pr-smoke)")
    parser.add_argument("--eest-root", default=None,
                        help="EVM_REF_EEST_ROOT (parent of fixtures/); default from CMakeCache")
    parser.add_argument("--manifest", default=None,
                        help="smoke manifest.txt (informational; default: <build>/eest-pr-smoke/"
                             "manifest.txt)")
    parser.add_argument("--self-test", action="store_true",
                        help="run the synthetic gate self-test and exit")
    args = parser.parse_args(argv)

    # Line-buffer stdout so our progress printouts precede the (inherited-fd)
    # eest-runner output even when this script's stdout is redirected to a file.
    sys.stdout.reconfigure(line_buffering=True)

    if args.self_test:
        return self_test()

    repo = repo_root()
    cache = read_cmake_cache(repo / "build" / "CMakeCache.txt")
    build_dir = pathlib.Path(cache.get("CMAKE_BINARY_DIR", str(repo / "build")))
    fixture_root = args.eest_root or cache.get("EVM_REF_EEST_ROOT")

    runner = args.runner or os.environ.get("EEST_RUNNER")
    if not runner:
        cand = build_dir / "ethereum-executor" / "tests" / "eest-runner"
        if cand.is_file():
            runner = str(cand)
    if not runner or not pathlib.Path(runner).is_file():
        print(f"[regress_baseline] ERROR: eest-runner not found; pass --runner or set "
              f"EEST_RUNNER (looked for {runner or 'nothing'})", file=sys.stderr)
        return 2

    baseline_path = args.baseline or str(repo / BASELINE_REL)
    json_out = pathlib.Path(args.json_failures) if args.json_failures else None
    tmp_json = None
    if json_out is None:
        fd, tmp_name = tempfile.mkstemp(prefix="regress_baseline_fails_", suffix=".json")
        os.close(fd)
        tmp_json = pathlib.Path(tmp_name)
        json_out = tmp_json

    env = dict(os.environ)
    if fixture_root:
        env["EEST_FIXTURE_DIR"] = str(pathlib.Path(fixture_root) / "fixtures")

    # --- Resolve the target -------------------------------------------------
    # Remove any stale json output first so the post-run content is guaranteed
    # to come from THIS run (eest-runner early-returns before writing json when
    # e.g. the fixture directory is missing, which would otherwise leave a
    # stale/0-byte file behind).
    json_out.unlink(missing_ok=True)
    runner_rc = None
    label = ""
    if args.fixture_dir:
        cmd_args = ["--fixture-dir", args.fixture_dir]
        label = args.fixture_dir
        runner_rc = run_runner(runner, cmd_args, json_out, env)
    elif args.full:
        if not fixture_root:
            print("[regress_baseline] ERROR: --full needs EVM_REF_EEST_ROOT "
                  "(pass --eest-root or configure the build)", file=sys.stderr)
            return 2
        full_dir = str(pathlib.Path(fixture_root) / "fixtures" / "state_tests")
        cmd_args = ["--fixture-dir", full_dir]
        label = full_dir
        runner_rc = run_runner(runner, cmd_args, json_out, env)
    elif args.fixture:
        label = " ".join(args.fixture)
        # run_multiple_fixtures writes json itself; treat "no output" as infra.
        merged_ok = run_multiple_fixtures(runner, args.fixture, json_out, env)
        if not merged_ok:
            if tmp_json is not None:
                tmp_json.unlink(missing_ok=True)
            return 2
    else:
        smoke_dir = args.smoke_dir or str(build_dir / "eest-pr-smoke")
        manifest_path = args.manifest or str(pathlib.Path(smoke_dir) / SMOKE_MANIFEST_REL)
        smoke_fixtures = read_smoke_manifest(manifest_path)
        print(f"[regress_baseline] smoke subset ({len(smoke_fixtures)} fixture files):")
        for f in smoke_fixtures:
            print(f"  - {pathlib.Path(f).name}")
        cmd_args = ["--fixture-dir", smoke_dir]
        label = smoke_dir
        runner_rc = run_runner(runner, cmd_args, json_out, env)

    # eest-runner exits 0 (all pass) or 1 (some failures).  Any other code means
    # the runner itself crashed — an infrastructure error, not a regression.
    if runner_rc is not None and runner_rc not in (0, 1):
        print(f"[regress_baseline] ERROR: eest-runner crashed (returncode={runner_rc}, "
              f"target: {label}); see output above", file=sys.stderr)
        if tmp_json is not None:
            tmp_json.unlink(missing_ok=True)
        return 2

    # A legit run always writes a --json-failures array (empty "[]" when 0
    # failures).  A missing/empty output means the run never reached the
    # writer (e.g. fixture directory not found) — fail loud instead of
    # silently scoring "no failures".
    if not json_out.is_file() or not json_out.read_text(encoding="utf-8",
                                                        errors="replace").strip():
        print(f"[regress_baseline] ERROR: eest-runner produced no --json-failures output "
              f"(target: {label})", file=sys.stderr)
        if tmp_json is not None:
            tmp_json.unlink(missing_ok=True)
        return 2

    new_records = load_json(json_out)
    baseline_records = load_json(baseline_path)
    new_failures = compute_new_failures(new_records, baseline_records)
    print_report(new_failures, len(baseline_records), len(new_records), baseline_path)

    if tmp_json is not None:
        tmp_json.unlink(missing_ok=True)
    return exit_code_for(new_failures)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
