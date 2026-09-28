# Copyright (c) FISCO-BCOS, Apache-2.0
"""Synthetic-input tests for compare.py. Log lines use the JSON shape kona-host v1.7.0 prints
with `--logs.stdout.format json` ({"level", "fields": {"message", ...}, "target"})."""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import compare  # noqa: E402

R = {k: "0x" + c * 64 for k, c in (("a", "a"), ("b", "b"), ("c", "c"), ("d", "d"), ("e", "e"))}


def line(level, message, **fields):
    return json.dumps({"timestamp": "t", "level": level,
                       "fields": {"message": message, **fields}, "target": "x"})


def sealed(n, state=R["a"], receipts=R["b"]):
    return line("INFO", "Sealed new block", number=n, hash=R["c"], state_root=state,
                transactions_root=R["d"], receipts_root=receipts)


def ok_log(n, root, hints=2):
    return ([line("WARN", f"L2StateNode hint was sent for node hash: {R['e']}")] * hints
            + [line("ERROR", "Failed to prefetch high-level hint: debug_executePayload failed: "
                    "Method not found"),
               sealed(n),
               line("INFO", "Successfully validated L2 block", number=n, output_root=root)])


def bad_log(n, kona_root, claimed):
    return [sealed(n, state=R["e"]),
            line("ERROR", "Failed to validate L2 block", number=n, output_root=kona_root,
                 claimed_output_root=claimed)]


def block(n, *txs):
    return {"number": hex(n), "hash": R["c"], "stateRoot": R["a"], "receiptsRoot": R["b"],
            "transactionsRoot": R["d"], "withdrawalsRoot": R["e"], "transactions": list(txs)}


L1INFO = {"type": "0x7e", "from": compare.L1_INFO_DEPOSITOR, "to": "0x42", "status": "0x1"}
DEPOSIT = {"type": "0x7e", "from": "0x7099", "to": "0x7099", "status": "0x1"}
SETCODE = {"type": "0x4", "from": "0xa0ee", "to": "0xa0ee", "status": "0x1"}
REVERT = {"type": "0x0", "from": "0x7099", "to": "0x5fbd", "status": "0x0"}
OVERLAY = {"type": "0x0", "from": "0x7099", "to": compare.OVERLAY_SYSTEM_CONFIG, "status": "0x1"}
FULL_MIX = [block(1, L1INFO), block(2, L1INFO, DEPOSIT), block(3, L1INFO, SETCODE, REVERT),
            block(4, L1INFO, OVERLAY)]


def test_parse_extracts_roots_hints_and_keeps_crashes():
    k = compare.parse_kona_log(ok_log(7, R["a"]) + ["thread 'main' panicked at x.rs:1"])
    assert k["sealed"][7]["stateRoot"] == R["a"]
    assert k["validated"] == {"number": 7, "output_root": R["a"]}
    assert k["state_node_hints"] == 2 and k["witness_failures"] == 1
    assert any("panicked" in e for e in k["errors"])
    assert not any("debug_executePayload" in e for e in k["errors"])


def test_verdict_match():
    v = compare.block_verdict(7, 0, compare.parse_kona_log(ok_log(7, R["a"])), R["a"].upper())
    assert v["verdict"] == "MATCH"


def test_verdict_mismatch_carries_kona_root():
    v = compare.block_verdict(7, 1, compare.parse_kona_log(bad_log(7, R["b"], R["a"])), R["a"])
    assert v["verdict"] == "MISMATCH" and v["kona_output_root"] == R["b"]


def test_verdict_halt_before_target_is_error_not_mismatch():
    log = [line("WARN", "Exhausted data source; Halting derivation and using current safe head."),
           line("ERROR", "Failed to validate L2 block", number=6, output_root=R["b"],
                claimed_output_root=R["a"])]
    v = compare.block_verdict(7, 1, compare.parse_kona_log(log), R["a"])
    assert v["verdict"] == "ERROR" and "halted at block 6" in v["reason"]


@pytest.mark.parametrize("rc,log", [(1, ["not json, error: connection refused"]), (0, [])])
def test_verdict_without_validation_line_is_error(rc, log):
    v = compare.block_verdict(7, rc, compare.parse_kona_log(log), R["a"])
    assert v["verdict"] == "ERROR"


def test_coverage_full_mix_and_gaps():
    assert compare.coverage_gaps(FULL_MIX, "on") == []
    assert compare.coverage_gaps(FULL_MIX[:3], "off") == []
    gaps = compare.coverage_gaps([block(1, L1INFO), block(2, SETCODE)], "on")
    assert gaps == ["user deposit (0x7e)", "reverting call", "call into overlay SystemConfig"]


def run_summary(logs_by_block, verdict_rcs, expect=False, blocks=FULL_MIX):
    logs = {b: compare.parse_kona_log(l) for b, l in logs_by_block.items()}
    verdicts = [compare.block_verdict(b, rc, logs[b], R["a"]) for b, rc in verdict_rcs]
    return compare.summarize(verdicts, blocks, logs, "on", expect)


def test_summary_all_match_is_green():
    code, lines = run_summary({b: ok_log(b, R["a"]) for b in (1, 2, 3, 4)},
                              [(1, 0), (2, 0), (3, 0), (4, 0)])
    assert code == compare.EXIT_MATCH and lines[-1].startswith("ALL 4 BLOCKS MATCH")


def test_summary_first_divergence_reports_both_sides():
    code, lines = run_summary({1: ok_log(1, R["a"]), 2: bad_log(2, R["b"], R["a"])},
                              [(1, 0), (2, 1)])
    text = "\n".join(lines)
    assert code == compare.EXIT_MISMATCH
    assert "FIRST DIVERGENCE at L2 block 2" in text
    state = next(l for l in lines[-1].splitlines() if l.startswith("!=stateRoot"))
    assert state.split()[1:] == [R["a"], R["e"]]
    assert "not logged by kona v1.7.0" in text


def test_summary_error_and_unproven_route_are_red():
    code, _ = run_summary({1: []}, [(1, 1)])
    assert code == compare.EXIT_ERROR
    code, lines = run_summary({b: ok_log(b, R["a"], hints=0) for b in (1, 2, 3, 4)},
                              [(1, 0), (2, 0), (3, 0), (4, 0)])
    assert code == compare.EXIT_ERROR and "debug_dbGet" in lines[-1]
    code, lines = run_summary({1: ok_log(1, R["a"])}, [(1, 0)], blocks=FULL_MIX[:1])
    assert code == compare.EXIT_ERROR and "lacks" in lines[-1]
    assert compare.summarize([], FULL_MIX, {}, "on", False)[0] == compare.EXIT_ERROR


def test_negative_control_expects_first_fee_paying_block():
    assert compare.first_fee_paying_block(FULL_MIX) == 3
    code, lines = run_summary({3: bad_log(3, R["b"], R["a"])}, [(3, 1)], expect=True)
    assert code == compare.EXIT_MISMATCH and "NEGATIVE CONTROL OK" in lines[-1]
    code, lines = run_summary({4: bad_log(4, R["b"], R["a"])}, [(4, 1)], expect=True)
    assert code == compare.EXIT_ERROR and "WRONG" in lines[-1]
    code, lines = run_summary({b: ok_log(b, R["a"]) for b in (1, 2, 3, 4)},
                              [(1, 0), (2, 0), (3, 0), (4, 0)], expect=True)
    assert code == compare.EXIT_MATCH
    assert any("NEGATIVE CONTROL FAILED" in l for l in lines)


def test_cli_verdict_and_summary_exit_codes(tmp_path):
    (tmp_path / "kona").mkdir()
    (tmp_path / "fisco_blocks.json").write_text(json.dumps(FULL_MIX))
    for b, log in ((1, ok_log(1, R["a"])), (2, bad_log(2, R["b"], R["a"]))):
        (tmp_path / "kona" / f"{b}.log").write_text("\n".join(log))
    common = ["--workdir", str(tmp_path), "--claimed", R["a"]]
    assert compare.main(["verdict", "--block", "1", "--rc", "0",
                         "--log", str(tmp_path / "kona/1.log")] + common) == 0
    assert compare.main(["verdict", "--block", "2", "--rc", "1",
                         "--log", str(tmp_path / "kona/2.log")] + common) == 1
    assert compare.main(["summary", "--workdir", str(tmp_path), "--overlay", "on"]) == 1
    assert compare.main(["coverage", "--workdir", str(tmp_path), "--overlay", "on"]) == 0


def test_cli_exception_is_error_not_mismatch():
    rc = compare.main(["collect", "--l2", "http://127.0.0.1:1", "--op-node",
                       "http://127.0.0.1:1", "--first", "1", "--last", "2", "--out", "/nonexist",
                       "--wait-safe", "0"])
    assert rc == compare.EXIT_ERROR


def test_beacon_stub_serves_startup_endpoints():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    proc = subprocess.Popen([sys.executable, compare.__file__, "beacon-stub", "--port", str(port),
                             "--genesis-time", "1790585366", "--seconds-per-slot", "1"])
    try:
        url = f"http://127.0.0.1:{port}/eth/v1/beacon/genesis"
        for _ in range(50):
            try:
                body = json.load(urllib.request.urlopen(url, timeout=1))
                break
            except OSError:
                time.sleep(0.1)
        assert int(body["data"]["genesis_time"], 16) == 1790585366
        spec = json.load(urllib.request.urlopen(
            f"http://127.0.0.1:{port}/eth/v1/config/spec", timeout=1))
        assert int(spec["data"]["SECONDS_PER_SLOT"], 16) == 1
        with pytest.raises(urllib.error.HTTPError):
            urllib.request.urlopen(f"http://127.0.0.1:{port}/eth/v1/beacon/blobs/1", timeout=1)
    finally:
        proc.kill()
