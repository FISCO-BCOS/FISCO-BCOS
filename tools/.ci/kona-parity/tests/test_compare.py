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


WITNESS_REFUSED = line("ERROR", "Failed to prefetch high-level hint: debug_executePayload failed: "
                       "server returned an error response: error code -32601: Method not found")


def ok_log(n, root, witness=1, code=0):
    return ([line("TRACE", f"Received hint: l2-code {R['e']}")] * code
            + [WITNESS_REFUSED] * witness
            + [sealed(n),
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
    k = compare.parse_kona_log(ok_log(7, R["a"], witness=2, code=3)
                               + ["thread 'main' panicked at x.rs:1"])
    assert k["sealed"][7]["stateRoot"] == R["a"]
    assert k["validated"] == {"number": 7, "output_root": R["a"]}
    assert k["witness_failures"] == 2 and k["code_hints"] == 3 and k["state_node_hints"] == 0
    assert any("panicked" in e for e in k["errors"])
    assert not any("debug_executePayload" in e for e in k["errors"])


def test_verdict_match():
    v = compare.block_verdict(7, 0, compare.parse_kona_log(ok_log(7, R["a"])), R["a"].upper())
    assert v["verdict"] == "MATCH"


def test_verdict_match_without_refused_witness_is_error():
    v = compare.block_verdict(7, 0, compare.parse_kona_log(ok_log(7, R["a"], witness=0)), R["a"])
    assert v["verdict"] == "ERROR" and "debug_executePayload" in v["reason"]


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


def run_summary(logs_by_block, verdict_rcs, expect=False, blocks=FULL_MIX, gated=None):
    logs = {b: compare.parse_kona_log(l) for b, l in logs_by_block.items()}
    verdicts = [compare.block_verdict(b, rc, logs[b], R["a"]) for b, rc in verdict_rcs]
    if gated is None:
        gated = {b for b, _ in verdict_rcs}  # the replay covered exactly what it scheduled
    return compare.summarize(verdicts, blocks, logs, "on", expect, gated)


def test_summary_all_match_is_green():
    # Zero state-node hints is the normal case (kona reads state via eth_getProof); the
    # debug_dbGet code-hint count is reported, never required.
    code, lines = run_summary({b: ok_log(b, R["a"], code=2) for b in (1, 2, 3, 4)},
                              [(1, 0), (2, 0), (3, 0), (4, 0)])
    assert code == compare.EXIT_MATCH and lines[-1].startswith("ALL 4 BLOCKS MATCH")
    assert "8 code hints" in lines[-1] and "0 state-node hints" in lines[-1]


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
    code, lines = run_summary({1: ok_log(1, R["a"], witness=0)}, [(1, 0)])
    assert code == compare.EXIT_ERROR and "debug_executePayload" in lines[-1]
    code, lines = run_summary({1: ok_log(1, R["a"])}, [(1, 0)], blocks=FULL_MIX[:1])
    assert code == compare.EXIT_ERROR and "lacks" in lines[-1]
    assert compare.summarize([], FULL_MIX, {}, "on", False, {1})[0] == compare.EXIT_ERROR


ALL_OK = {b: ok_log(b, R["a"]) for b in (1, 2, 3, 4)}


def test_summary_truncated_verdicts_are_incomplete_evidence():
    # verdicts.jsonl holds a prefix of pairs.tsv (interrupted replay): the mix is fully
    # covered by the block file, yet blocks 3 and 4 were never replayed.
    code, lines = run_summary(ALL_OK, [(1, 0), (2, 0)], gated={1, 2, 3, 4})
    assert code == compare.EXIT_ERROR
    assert lines[-1] == "GATE ERROR: incomplete evidence: no verdict for gated blocks [3, 4]"


def test_summary_verdicts_outside_pairs_are_incomplete_evidence():
    code, lines = run_summary(ALL_OK, [(1, 0), (2, 0), (3, 0), (4, 0)], gated={1, 2, 3})
    assert code == compare.EXIT_ERROR
    assert "verdicts for blocks outside pairs.tsv [4]" in lines[-1]


def test_summary_duplicate_verdicts_are_incomplete_evidence():
    code, lines = run_summary(ALL_OK, [(1, 0), (2, 0), (2, 0), (3, 0), (4, 0)],
                              gated={1, 2, 3, 4})
    assert code == compare.EXIT_ERROR
    assert "more than one verdict for blocks [2]" in lines[-1]


def test_summary_exact_coverage_is_green_and_mismatch_prefix_stays_red():
    code, _ = run_summary(ALL_OK, [(1, 0), (2, 0), (3, 0), (4, 0)], gated={1, 2, 3, 4})
    assert code == compare.EXIT_MATCH
    # run.sh stops at the first non-MATCH, so a mismatch prefix is complete evidence of red.
    code, lines = run_summary({1: ok_log(1, R["a"]), 2: bad_log(2, R["b"], R["a"])},
                              [(1, 0), (2, 1)], gated={1, 2, 3, 4})
    assert code == compare.EXIT_MISMATCH and "FIRST DIVERGENCE at L2 block 2" in lines[-1]


def test_clean_removes_run_owned_files_only(tmp_path):
    stale = ["verdicts.jsonl", "pairs.tsv", "fisco_blocks.json", "outputs.json",
             "l1_head.txt", "rollup.json", "l1-config.json", "setup_c2.log", "beacon-stub.log"]
    for name in stale:
        (tmp_path / name).write_text("stale")
    (tmp_path / "kona").mkdir()
    (tmp_path / "kona" / "5.log").write_text("stale")
    (tmp_path / "kona" / "5.kv").mkdir()
    (tmp_path / "c2" / "fisco").mkdir(parents=True)
    (tmp_path / "keep.txt").write_text("mine")
    assert compare.main(["clean", "--workdir", str(tmp_path)]) == 0
    assert sorted(p.name for p in tmp_path.iterdir()) == ["keep.txt"]
    assert compare.main(["clean", "--workdir", str(tmp_path)]) == 0  # idempotent
    assert set(stale + ["kona", "c2"]) == set(compare.RUN_OWNED)


def test_cli_summary_reads_gated_set_from_pairs_tsv(tmp_path):
    (tmp_path / "kona").mkdir()
    (tmp_path / "fisco_blocks.json").write_text(json.dumps(FULL_MIX))
    (tmp_path / "pairs.tsv").write_text("".join(f"{b}\t{R['c']}\t{R['a']}\t{R['a']}\n"
                                                for b in (1, 2, 3, 4)))
    for b in (1, 2, 3):  # a stale verdicts.jsonl from a run that was interrupted before 4
        (tmp_path / "kona" / f"{b}.log").write_text("\n".join(ok_log(b, R["a"])))
        assert compare.main(["verdict", "--workdir", str(tmp_path), "--block", str(b),
                             "--rc", "0", "--log", str(tmp_path / f"kona/{b}.log"),
                             "--claimed", R["a"]]) == 0
    assert compare.main(["summary", "--workdir", str(tmp_path), "--overlay", "on"]) == 2
    (tmp_path / "kona" / "4.log").write_text("\n".join(ok_log(4, R["a"])))
    assert compare.main(["verdict", "--workdir", str(tmp_path), "--block", "4", "--rc", "0",
                         "--log", str(tmp_path / "kona/4.log"), "--claimed", R["a"]]) == 0
    assert compare.main(["summary", "--workdir", str(tmp_path), "--overlay", "on"]) == 0
    (tmp_path / "pairs.tsv").unlink()
    assert compare.main(["summary", "--workdir", str(tmp_path), "--overlay", "on"]) == 2


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
    (tmp_path / "pairs.tsv").write_text("".join(f"{b}\t{R['c']}\t{R['a']}\t{R['a']}\n"
                                                for b in (1, 2, 3, 4)))
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


# ---------------------------------------------------------------- preflight / origin-lag, over a
# fake JSON-RPC node whose answers are switched per test.

GENESIS_RLP = "0xc0"  # keccak(0xc0) is the hash the fake node reports for block 0
LATEST_RLP = "0xc180"


class FakeNode:
    def __init__(self, overrides=None):
        from eth_hash.auto import keccak
        h = lambda raw: "0x" + keccak(bytes.fromhex(raw[2:])).hex()  # noqa: E731
        self.node = "0x" + "01" * 8
        self.answers = {
            ("eth_getBlockByNumber", "0x0"): {"result": {"hash": h(GENESIS_RLP), "stateRoot": h(self.node)}},
            ("eth_getBlockByNumber", "latest"): {"result": {"hash": h(LATEST_RLP), "stateRoot": h(self.node)}},
            ("debug_getRawHeader", h(GENESIS_RLP)): {"result": GENESIS_RLP},
            ("debug_getRawHeader", h(LATEST_RLP)): {"result": LATEST_RLP},
            ("debug_getRawReceipts", "latest"): {"result": []},
            ("debug_dbGet", h(self.node)): {"result": self.node},
            ("eth_getProof", compare.ABSENT_ACCOUNT): {"result": {"accountProof": ["0x80"]}},
            ("debug_executePayload", None): {"error": {"code": -32601, "message": "not found"}},
            ("optimism_syncStatus", None): {"result": {"head_l1": {"number": 400},
                                                       "unsafe_l2": {"l1origin": {"number": 380}}}},
        }
        self.answers.update(overrides or {})

    def __call__(self, url, method, params):
        key = params[0] if params and isinstance(params[0], str) else None
        return self.answers.get((method, key)) or self.answers.get((method, None)) \
            or {"error": {"code": -32601, "message": "Method not found"}}


def preflight(monkeypatch, capsys, overrides=None):
    monkeypatch.setattr(compare, "rpc", FakeNode(overrides))
    rc = compare.main(["preflight", "--l1", "http://l1", "--l2", "http://l2"])
    return rc, capsys.readouterr().out


def test_preflight_passes_on_a_complete_node(monkeypatch, capsys):
    assert preflight(monkeypatch, capsys) == (compare.EXIT_MATCH, "")


def test_preflight_catches_refused_genesis_header(monkeypatch, capsys):
    from eth_hash.auto import keccak
    g = "0x" + keccak(bytes.fromhex(GENESIS_RLP[2:])).hex()
    rc, out = preflight(monkeypatch, capsys, {("debug_getRawHeader", g): {
        "error": {"code": -32603, "message": "Block 0 has no OP Ethereum header"}}})
    assert rc == compare.EXIT_ERROR
    assert "missing: L2 debug_getRawHeader (ticket 02: FISCO must serve the genesis header)" in out


def test_preflight_catches_missing_exclusion_proof(monkeypatch, capsys):
    rc, out = preflight(monkeypatch, capsys, {("eth_getProof", compare.ABSENT_ACCOUNT): {
        "error": {"code": -32004, "message": "Account not in trie"}}})
    assert rc == compare.EXIT_ERROR and "missing: L2 eth_getProof exclusion proof" in out


def test_preflight_requires_minus_32601_for_execute_payload(monkeypatch, capsys):
    rc, out = preflight(monkeypatch, capsys, {("debug_executePayload", None): {
        "error": {"code": -32603, "message": "internal"}}})
    assert rc == compare.EXIT_ERROR and "must answer -32601" in out
    rc, _ = preflight(monkeypatch, capsys, {("debug_executePayload", None): {"result": {}}})
    assert rc == compare.EXIT_ERROR


@pytest.mark.parametrize("head,origin,rc", [(400, 380, 0), (400, 250, 0), (900, 500, 2)])
def test_origin_lag_limit_is_budget_over_l2_block_time(monkeypatch, capsys, head, origin, rc):
    node = FakeNode({("optimism_syncStatus", None): {"result": {
        "head_l1": {"number": head}, "unsafe_l2": {"l1origin": {"number": origin}}}}})
    monkeypatch.setattr(compare, "rpc", node)
    assert compare.main(["origin-lag", "--op-node", "http://n", "--l2-block-time", "2",
                         "--budget", "300"]) == rc
