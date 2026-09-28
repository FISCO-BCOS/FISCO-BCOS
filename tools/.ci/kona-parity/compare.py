#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Evidence collection and verdict logic for the kona-client block parity gate.

run.sh owns the processes (devnet, beacon stub, kona-host); this file owns every decision:
which blocks are gated, what a kona run proved, and which exit code the gate returns.

Exit codes (every subcommand):
  0  MATCH / check passed
  1  MISMATCH: kona derived a different output root than FISCO for the same block
  2  ERROR: the gate could not produce evidence (missing tool, kona crash, coverage gap,
     unproven preimage route). A gate that cannot run is red, never green-by-skip.
"""
import argparse
import http.server
import json
import sys
import time
import urllib.request

EXIT_MATCH, EXIT_MISMATCH, EXIT_ERROR = 0, 1, 2

# Sender of the L1-info deposit every OP block starts with; a USER deposit has another sender.
L1_INFO_DEPOSITOR = "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001"
# FISCO overlay SystemConfig proxy (op-stack-e2e-tests tools/opstack-genesis/chain-config-c2.yaml).
OVERLAY_SYSTEM_CONFIG = "0x4200000000000000000000000000000000001000"
# An account no devnet ever touches: its eth_getProof must be an exclusion proof.
ABSENT_ACCOUNT = "0x000000000000000000000000000000000000dEaD"

# kona v1.7.0 log messages this gate keys on (rust/kona at optimism 64b043ea5bbc):
MSG_SEALED = "Sealed new block"  # crates/proof/executor/src/builder/core.rs:336-344
MSG_VALIDATED = "Successfully validated L2 block"  # bin/client/src/single.rs:151-156
MSG_FAILED = "Failed to validate L2 block"  # bin/client/src/single.rs:140-148
MSG_EXHAUSTED = "Exhausted data source"  # crates/proof/driver/src/core.rs:259
MSG_STATE_NODE = "L2StateNode hint was sent"  # bin/host/src/single/handler.rs:291
# Logged by the retry loop each time the retained L2PayloadWitness hint fails
# (bin/host/src/backend/online.rs:160, message suffix from backend/util.rs:29). A successful
# witness fetch logs nothing, so "kona validated the block AND this line appeared" is the only
# log evidence that kona's preimages came from the geth route, not from debug_executePayload.
MSG_WITNESS_FAILED = "Failed to prefetch high-level hint: debug_executePayload failed"
# trace-level (kona-host -vvvvv) hint routing, online.rs:109; hint name from
# crates/proof/proof/src/hint.rs:162. One line per code preimage kona asked FISCO's debug_dbGet for.
MSG_CODE_HINT = "Received hint: l2-code"


def _hex(value):
    return value.lower() if isinstance(value, str) else value


def _int(value):
    return value if isinstance(value, int) else int(str(value), 0)


# ---------------------------------------------------------------------------- pure logic


def parse_kona_log(lines):
    """Extract what one kona-host run proved from its `--logs.stdout.format json` output.

    Non-JSON lines (docker noise, panics) are kept as errors so a crash is never silent."""
    out = {"sealed": {}, "validated": None, "failed": None, "exhausted": False,
           "state_node_hints": 0, "witness_failures": 0, "code_hints": 0, "errors": []}
    for raw in lines:
        raw = raw.strip()
        if not raw:
            continue
        try:
            rec = json.loads(raw)
            fields = rec.get("fields", {})
            msg = str(fields.get("message", ""))
        except (ValueError, AttributeError):
            if "panicked" in raw or "error" in raw.lower():
                out["errors"].append(raw[:300])
            continue
        if msg == MSG_SEALED:
            out["sealed"][_int(fields["number"])] = {
                "hash": _hex(fields.get("hash")),
                "stateRoot": _hex(fields.get("state_root")),
                "transactionsRoot": _hex(fields.get("transactions_root")),
                "receiptsRoot": _hex(fields.get("receipts_root")),
            }
        elif msg == MSG_VALIDATED:
            out["validated"] = {"number": _int(fields["number"]),
                                "output_root": _hex(fields["output_root"])}
        elif msg == MSG_FAILED:
            out["failed"] = {"number": _int(fields["number"]),
                             "output_root": _hex(fields["output_root"]),
                             "claimed_output_root": _hex(fields["claimed_output_root"])}
        elif msg.startswith(MSG_EXHAUSTED):
            out["exhausted"] = True
        if msg.startswith(MSG_STATE_NODE):
            out["state_node_hints"] += 1
        if msg.startswith(MSG_CODE_HINT):
            out["code_hints"] += 1
        if msg.startswith(MSG_WITNESS_FAILED):
            out["witness_failures"] += 1
        elif rec.get("level") == "ERROR" and len(out["errors"]) < 5:
            out["errors"].append(msg[:300])
    return out


def block_verdict(block, rc, kona, claimed_output_root):
    """Classify one kona run that claimed `block` against FISCO's output root."""
    claimed = _hex(claimed_output_root)
    v = {"block": block, "rc": rc, "fisco_output_root": claimed, "kona_output_root": None}
    failed, validated = kona["failed"], kona["validated"]
    if failed is not None:
        if kona["exhausted"] or failed["number"] != block:
            # kona stopped before reaching `block`: an L1-head/batch problem, not an
            # execution difference, so it must not be reported as a divergence.
            v.update(verdict="ERROR", reason=f"kona halted at block {failed['number']} before "
                     f"reaching {block} (L1 head too early or batch not on L1)")
        else:
            v.update(verdict="MISMATCH", kona_output_root=failed["output_root"],
                     reason="kona derived a different output root")
        return v
    if rc == 0 and validated is not None and validated["number"] == block:
        v["kona_output_root"] = validated["output_root"]
        if validated["output_root"] != claimed:
            v.update(verdict="MISMATCH", reason="validated root differs from claim")
        elif kona["witness_failures"] == 0:
            v.update(verdict="ERROR", reason="kona validated the block but logged no refused "
                     "debug_executePayload: cannot show the witness route was unused")
        else:
            v.update(verdict="MATCH", reason="")
        return v
    detail = "; ".join(kona["errors"][:3]) or "no validation line in kona log"
    v.update(verdict="ERROR", reason=f"kona-host exit {rc}: {detail}")
    return v


def coverage_gaps(blocks, overlay):
    """Names of required transaction kinds absent from `blocks` (FISCO eth_getBlockByNumber
    results with a per-tx receipt `status`). Empty list = the mix is covered."""
    seen = {"user deposit (0x7e)": False, "EIP-7702 (0x4)": False, "reverting call": False}
    if overlay == "on":
        seen["call into overlay SystemConfig"] = False
    for b in blocks:
        for tx in b.get("transactions", []):
            if tx.get("type") == "0x7e" and _hex(tx.get("from")) != L1_INFO_DEPOSITOR:
                seen["user deposit (0x7e)"] = True
            if tx.get("type") == "0x4" and tx.get("status") == "0x1":
                seen["EIP-7702 (0x4)"] = True
            if tx.get("status") == "0x0":
                seen["reverting call"] = True
            if overlay == "on" and _hex(tx.get("to")) == OVERLAY_SYSTEM_CONFIG \
                    and tx.get("status") == "0x1":
                seen["call into overlay SystemConfig"] = True
    return [k for k, ok in seen.items() if not ok]


def first_fee_paying_block(blocks):
    """First block holding a non-deposit tx: the first one that credits the base fee vault."""
    for b in sorted(blocks, key=lambda x: _int(x["number"])):
        if any(tx.get("type") != "0x7e" for tx in b.get("transactions", [])):
            return _int(b["number"])
    return None


def render_divergence(verdict, fisco_block, kona):
    """Side-by-side header roots of the first divergent block."""
    sealed = kona["sealed"].get(verdict["block"], {})
    rows = [("outputRoot", verdict["fisco_output_root"], verdict["kona_output_root"])]
    for key in ("hash", "stateRoot", "receiptsRoot", "transactionsRoot"):
        rows.append((key, _hex(fisco_block.get(key)), sealed.get(key, "not in kona log")))
    rows.append(("withdrawalsRoot", _hex(fisco_block.get("withdrawalsRoot")),
                 "not logged by kona v1.7.0"))
    lines = [f"FIRST DIVERGENCE at L2 block {verdict['block']}",
             f"  {'field':<17} {'FISCO':<68} kona-client"]
    for name, f, k in rows:
        mark = "  " if f == k else "!="
        lines.append(f"{mark}{name:<17} {str(f):<68} {k}")
    return "\n".join(lines)


def summarize(verdicts, blocks, kona_logs, overlay, expect_fee_vault):
    """Final gate verdict. Returns (exit_code, report_lines)."""
    lines = []
    for v in verdicts:
        lines.append(f"block {v['block']}: {v['verdict']} {v['reason']}".rstrip())
    by_number = {_int(b["number"]): b for b in blocks}
    bad = next((v for v in verdicts if v["verdict"] != "MATCH"), None)
    if bad is not None and bad["verdict"] == "MISMATCH":
        lines.append(render_divergence(bad, by_number.get(bad["block"], {}),
                                       kona_logs.get(bad["block"], parse_kona_log([]))))
        if expect_fee_vault:
            want = first_fee_paying_block(blocks)
            if bad["block"] != want:
                lines.append(f"NEGATIVE CONTROL WRONG: divergence at {bad['block']}, "
                             f"first fee-paying block is {want}")
                return EXIT_ERROR, lines
            lines.append(f"NEGATIVE CONTROL OK: diverged at first fee-paying block {want}")
        return EXIT_MISMATCH, lines
    if bad is not None:
        lines.append(f"GATE ERROR at block {bad['block']}: {bad['reason']}")
        return EXIT_ERROR, lines
    if not verdicts:
        lines.append("GATE ERROR: no block was replayed")
        return EXIT_ERROR, lines
    gaps = coverage_gaps(blocks, overlay)
    if gaps:
        lines.append(f"GATE ERROR: gated range lacks {', '.join(gaps)}")
        return EXIT_ERROR, lines
    # Informational only: kona v1.7.0 takes accounts and storage from eth_getProof and uses
    # debug_dbGet for bytecode; state-node hints appear only when a proof misses a node.
    code = sum(k["code_hints"] for k in kona_logs.values())
    nodes = sum(k["state_node_hints"] for k in kona_logs.values())
    if expect_fee_vault:
        lines.append("NEGATIVE CONTROL FAILED: mutated fee vault but every block matched")
    lines.append(f"ALL {len(verdicts)} BLOCKS MATCH kona-client (overlay={overlay}; "
                 f"debug_dbGet: {code} code hints [trace-level, 0 unless KONA_TRACE=1], "
                 f"{nodes} state-node hints)")
    return EXIT_MATCH, lines


# ---------------------------------------------------------------------------- RPC side


def rpc(url, method, params):
    req = urllib.request.Request(url, data=json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def rpc_result(url, method, params):
    body = rpc(url, method, params)
    if "error" in body:
        raise RuntimeError(f"{method} on {url}: {body['error']}")
    return body["result"]


def keccak_hex(data_hex):
    from eth_hash.auto import keccak
    return "0x" + keccak(bytes.fromhex(data_hex[2:])).hex()


def fetch_blocks(l2, first, last):
    blocks = []
    for n in range(first, last + 1):
        b = rpc_result(l2, "eth_getBlockByNumber", [hex(n), True])
        txs = []
        for tx in b.get("transactions", []):
            r = rpc_result(l2, "eth_getTransactionReceipt", [tx["hash"]])
            txs.append({"hash": tx["hash"], "type": tx.get("type"), "from": tx.get("from"),
                        "to": tx.get("to"), "status": r.get("status")})
        keep = {k: b.get(k) for k in ("number", "hash", "stateRoot", "receiptsRoot",
                                      "transactionsRoot", "withdrawalsRoot")}
        keep["transactions"] = txs
        blocks.append(keep)
    return blocks


def cmd_preflight(a):
    missing = []

    def probe_header(url, side, hint, tag):
        try:
            b = rpc_result(url, "eth_getBlockByNumber", [tag, False])
            raw = rpc_result(url, "debug_getRawHeader", [b["hash"]])
            if keccak_hex(raw) != b["hash"].lower():
                missing.append(f"{side} debug_getRawHeader({tag}) returns RLP whose keccak != "
                               "block hash")
            return b
        except Exception as e:  # noqa: BLE001 — every failure becomes a "missing:" line
            missing.append(f"{side} debug_getRawHeader ({hint}): {e}")
            return None

    probe_header(a.l1, "L1", "anvil from foundry >= v1.8.0", "latest")
    try:
        rpc_result(a.l1, "debug_getRawReceipts", ["latest"])
    except Exception as e:  # noqa: BLE001
        missing.append(f"L1 debug_getRawReceipts (anvil from foundry >= v1.8.0): {e}")
    # Block 0 separately: kona fetches the genesis header whenever the agreed block is near
    # genesis, and a FISCO build that served only post-genesis headers hung the first live run.
    probe_header(a.l2, "L2", "ticket 02: FISCO must serve the genesis header", "0x0")
    head = probe_header(a.l2, "L2", "ticket 02", "latest")
    if head is not None:
        try:
            node = rpc_result(a.l2, "debug_dbGet", [head["stateRoot"]])
            if keccak_hex(node) != head["stateRoot"].lower():
                missing.append("L2 debug_dbGet(stateRoot) returns bytes whose keccak != key")
        except Exception as e:  # noqa: BLE001
            missing.append(f"L2 debug_dbGet (ticket 02): {e}")
    # kona hints L2AccountProof for every account it reads (handler.rs:303-338); an account
    # absent from the trie must come back as an exclusion proof, not an error, or kona retries
    # the hint until the per-block timeout.
    try:
        proof = rpc_result(a.l2, "eth_getProof", [ABSENT_ACCOUNT, [], "latest"])
        if not isinstance(proof, dict) or not isinstance(proof.get("accountProof"), list):
            missing.append(f"L2 eth_getProof({ABSENT_ACCOUNT}) returned {str(proof)[:80]}, "
                           "not a proof object")
    except Exception as e:  # noqa: BLE001
        missing.append(f"L2 eth_getProof exclusion proof for an absent account "
                       f"({ABSENT_ACCOUNT}): {e}")
    body = rpc(a.l2, "debug_executePayload", ["0x" + "00" * 32, {}])
    code = body.get("error", {}).get("code") if isinstance(body.get("error"), dict) else None
    if code != -32601:
        missing.append(f"L2 debug_executePayload must answer -32601 (ADR 0007: the witness "
                       f"route stays unimplemented so kona uses the geth route); got {body}"[:200])
    for m in missing:
        print(f"missing: {m}")
    return EXIT_ERROR if missing else EXIT_MATCH


def cmd_wait_mix(a):
    deadline = time.time() + a.timeout
    while True:
        tip = int(rpc_result(a.l2, "eth_blockNumber", []), 16)
        blocks = fetch_blocks(a.l2, a.first, tip) if tip >= a.first else []
        gaps = coverage_gaps(blocks, a.overlay)
        if not gaps:
            last = max(_int(b["number"]) for b in blocks
                       if any(tx.get("type") != "0x7e" or _hex(tx.get("from")) != L1_INFO_DEPOSITOR
                              for tx in b["transactions"]))
            print(last)
            return EXIT_MATCH
        if time.time() > deadline:
            print(f"missing: transaction mix not included after {a.timeout}s: {', '.join(gaps)}",
                  file=sys.stderr)
            return EXIT_ERROR
        time.sleep(3)


def cmd_collect(a):
    deadline = time.time() + a.wait_safe
    while True:
        status = rpc_result(a.op_node, "optimism_syncStatus", [])
        if status["safe_l2"]["number"] >= a.last:
            break
        if time.time() > deadline:
            print(f"missing: safe_l2 {status['safe_l2']['number']} never reached {a.last} "
                  f"within {a.wait_safe}s (is op-batcher posting?)", file=sys.stderr)
            return EXIT_ERROR
        time.sleep(3)
    blocks = fetch_blocks(a.l2, a.first, a.last)
    outputs = {n: rpc_result(a.op_node, "optimism_outputAtBlock", [hex(n)])
               for n in range(a.first - 1, a.last + 1)}
    with open(f"{a.out}/fisco_blocks.json", "w") as f:
        json.dump(blocks, f, indent=1)
    with open(f"{a.out}/outputs.json", "w") as f:
        json.dump(outputs, f, indent=1)
    with open(f"{a.out}/pairs.tsv", "w") as f:
        for n in range(a.first, a.last + 1):
            prev = outputs[n - 1]
            f.write(f"{n}\t{prev['blockRef']['hash']}\t{prev['outputRoot']}\t"
                    f"{outputs[n]['outputRoot']}\n")
    # current_l1: the L1 block derivation has consumed; every block <= safe_l2 was derived
    # from L1 data at or below it, so kona can reach each claimed block from this head.
    with open(f"{a.out}/l1_head.txt", "w") as f:
        f.write(status["current_l1"]["hash"] + "\n")
    return EXIT_MATCH


def _load_run(workdir):
    with open(f"{workdir}/fisco_blocks.json") as f:
        blocks = json.load(f)
    verdicts, logs = [], {}
    try:
        with open(f"{workdir}/verdicts.jsonl") as f:
            verdicts = [json.loads(line) for line in f if line.strip()]
    except FileNotFoundError:
        pass
    for v in verdicts:
        with open(f"{workdir}/kona/{v['block']}.log") as f:
            logs[v["block"]] = parse_kona_log(f)
    return blocks, verdicts, logs


def cmd_verdict(a):
    with open(a.log) as f:
        kona = parse_kona_log(f)
    v = block_verdict(a.block, a.rc, kona, a.claimed)
    with open(f"{a.workdir}/verdicts.jsonl", "a") as f:
        f.write(json.dumps(v) + "\n")
    print(f"[kona-parity] block {a.block}: {v['verdict']} {v['reason']}".rstrip())
    return {"MATCH": EXIT_MATCH, "MISMATCH": EXIT_MISMATCH}.get(v["verdict"], EXIT_ERROR)


def cmd_coverage(a):
    blocks, _, _ = _load_run(a.workdir)
    gaps = coverage_gaps(blocks, a.overlay)
    for g in gaps:
        print(f"missing: gated range has no {g}")
    return EXIT_ERROR if gaps else EXIT_MATCH


def cmd_summary(a):
    blocks, verdicts, logs = _load_run(a.workdir)
    code, lines = summarize(verdicts, blocks, logs, a.overlay, a.expect_fee_vault)
    print("\n".join(lines))
    return code


def origin_lag(status):
    """L1 blocks between the L1 head and the sequencer's L1 origin (unsafe_l2.l1origin). A
    deposit made in the L1 head block is included once the origin reaches it, and the origin
    moves at most one L1 block per L2 block, so the deposit waits about lag * L2 block_time."""
    return status["head_l1"]["number"] - status["unsafe_l2"]["l1origin"]["number"]


def cmd_origin_lag(a):
    lag = origin_lag(rpc_result(a.op_node, "optimism_syncStatus", []))
    limit = a.budget // a.l2_block_time
    print(f"[kona-parity] sequencer L1-origin lag: {lag} L1 blocks "
          f"(deposit wait ~{lag * a.l2_block_time}s, limit {limit} blocks = {a.budget}s)")
    if lag > limit:
        print(f"missing: a devnet whose sequencer L1 origin trails the L1 head by <= {limit} "
              f"blocks (this one: {lag}); start a fresh devnet instead of --attach")
        return EXIT_ERROR
    return EXIT_MATCH


def cmd_beacon_stub(a):
    """Beacon API subset kona-host needs at startup (OnlineBlobProvider::init reads
    genesis_time and SECONDS_PER_SLOT; providers-alloy/src/blobs.rs:55-69). Blob requests get
    404: the devnet batcher posts calldata, so any blob request is a gate error."""
    bodies = {"/eth/v1/beacon/genesis": {"data": {"genesis_time": hex(a.genesis_time)}},
              "/eth/v1/config/spec": {"data": {"SECONDS_PER_SLOT": hex(a.seconds_per_slot)}}}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            body = bodies.get(self.path.split("?")[0])
            if body is None:
                print(f"beacon-stub: unexpected request {self.path}", file=sys.stderr)
                self.send_response(404)
                self.end_headers()
                return
            data = json.dumps(body).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *args):
            pass

    http.server.ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("preflight")
    s.add_argument("--l1", required=True)
    s.add_argument("--l2", required=True)
    s.set_defaults(fn=cmd_preflight)
    s = sub.add_parser("wait-mix")
    s.add_argument("--l2", required=True)
    s.add_argument("--first", type=int, required=True)
    s.add_argument("--overlay", choices=["on", "off"], required=True)
    s.add_argument("--timeout", type=int, default=600)
    s.set_defaults(fn=cmd_wait_mix)
    s = sub.add_parser("collect")
    s.add_argument("--l2", required=True)
    s.add_argument("--op-node", required=True)
    s.add_argument("--first", type=int, required=True)
    s.add_argument("--last", type=int, required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--wait-safe", type=int, default=900)
    s.set_defaults(fn=cmd_collect)
    s = sub.add_parser("verdict")
    s.add_argument("--workdir", required=True)
    s.add_argument("--block", type=int, required=True)
    s.add_argument("--rc", type=int, required=True)
    s.add_argument("--log", required=True)
    s.add_argument("--claimed", required=True)
    s.set_defaults(fn=cmd_verdict)
    for name, fn in (("coverage", cmd_coverage), ("summary", cmd_summary)):
        s = sub.add_parser(name)
        s.add_argument("--workdir", required=True)
        s.add_argument("--overlay", choices=["on", "off"], required=True)
        s.add_argument("--expect-fee-vault", action="store_true")
        s.set_defaults(fn=fn)
    s = sub.add_parser("origin-lag")
    s.add_argument("--op-node", required=True)
    s.add_argument("--l2-block-time", type=int, required=True)
    s.add_argument("--budget", type=int, required=True, help="seconds a deposit may take")
    s.set_defaults(fn=cmd_origin_lag)
    s = sub.add_parser("beacon-stub")
    s.add_argument("--port", type=int, required=True)
    s.add_argument("--genesis-time", type=int, required=True)
    s.add_argument("--seconds-per-slot", type=int, required=True)
    s.set_defaults(fn=cmd_beacon_stub)
    a = p.parse_args(argv)
    try:
        return a.fn(a)
    except Exception as e:  # noqa: BLE001 — an uncaught exception exits 1, which means MISMATCH
        print(f"[kona-parity] {a.cmd} failed: {type(e).__name__}: {e}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
