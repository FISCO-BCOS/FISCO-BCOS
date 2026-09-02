#!/usr/bin/env python3
"""c2lib — shared wait/assert primitives for the C2 e2e tool family.

One home for the polling idioms every script kept re-implementing:

  call_ok(argv...)      — cast-call exit status (a revert is a wait state)
  wait_until(...)       — poll a cast-call simulation until it succeeds;
                          NON-REVERT failures abort immediately with full argv
  heads(url)            — unsafe/safe/finalized from optimism_syncStatus
  wait_advance(...)     — poll until an L2 head field grows past a baseline

Python consumers import the functions; bash consumers use the CLI:

  c2lib.py heads [--url http://...]                 -> "U S F"
  c2lib.py wait-advance --field safe --baseline 100 \\
           --delta 3 --timeout 240 [--url ...]      -> exit 0/1

Endpoint defaults follow the family convention (C2_OP_NODE env override).
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.request

OP_NODE = None  # set in main(); module users pass urls explicitly


def call_ok(*args):
    # cast-call exit status — True iff the simulation succeeds, i.e. a clock
    # gate (game clock, proof maturity, finality delay) has expired. A revert
    # here is an expected wait state, never an error.
    return subprocess.run(["cast", "call", *args],
                          capture_output=True).returncode == 0


def wait_until(desc, timeout, *args):
    # Poll a cast-call simulation until it succeeds or the timeout (seconds)
    # elapses. Timeouts are generous: they bound chain-clock waits, not checks.
    # A CONTRACT REVERT is an expected wait state; anything else (cast parser
    # error, transport failure, bad args) is a defect in the invocation itself
    # — fail fast with the full command instead of burning the whole timeout
    # polling a bug (the 08-24 finalize chase lost 600s per run to exactly
    # that).
    deadline = time.time() + timeout
    last_err = ""
    while time.time() < deadline:
        r = subprocess.run(["cast", "call", *args], capture_output=True, text=True)
        if r.returncode == 0:
            return True
        last_err = (r.stderr or "").strip().splitlines()
        last_err = last_err[0][:160] if last_err else ""
        if "execution reverted" not in (r.stderr or ""):
            raise SystemExit(
                f"{desc}: cast call failed for a NON-REVERT reason — this is a "
                f"tool defect, not a clock gate.\n  argv: cast call "
                + " ".join(map(repr, args)) + f"\n  stderr: {(r.stderr or '').strip()[-500:]}")
        time.sleep(5)
    print(f"!! {desc} not ready within {timeout}s (last revert: {last_err or 'none'})")
    return False


def heads(url):
    # "unsafe safe finalized" from op-node syncStatus; -1s on any failure.
    try:
        req = urllib.request.Request(
            url, json.dumps({"jsonrpc": "2.0", "method": "optimism_syncStatus",
                             "params": [], "id": 1}).encode(),
            {"Content-Type": "application/json"})
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        s = json.loads(opener.open(req, timeout=5).read())["result"]
        return (s["unsafe_l2"]["number"], s["safe_l2"]["number"],
                s["finalized_l2"]["number"])
    except Exception:
        return (-1, -1, -1)


def wait_advance(url, field, baseline, delta, timeout):
    # Poll until the head field (0=unsafe 1=safe 2=finalized) reaches
    # baseline + delta. Monotonicity guards ("never regress") are asserted by
    # the CALLER comparing snapshots — this only waits for forward motion.
    deadline = time.time() + timeout
    while time.time() < deadline:
        if heads(url)[field] >= baseline + delta:
            return True
        time.sleep(5)
    return False


def main():
    import os
    default_url = os.environ.get("C2_OP_NODE", "http://127.0.0.1:9545")
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("heads")
    h.add_argument("--url", default=default_url)
    w = sub.add_parser("wait-advance")
    w.add_argument("--url", default=default_url)
    w.add_argument("--field", required=True, choices=["unsafe", "safe", "finalized"])
    w.add_argument("--baseline", type=int, required=True)
    w.add_argument("--delta", type=int, default=3)
    w.add_argument("--timeout", type=int, default=240)
    args = ap.parse_args()
    if args.cmd == "heads":
        print(" ".join(map(str, heads(args.url))))
    else:
        idx = {"unsafe": 0, "safe": 1, "finalized": 2}[args.field]
        sys.exit(0 if wait_advance(args.url, idx, args.baseline,
                                   args.delta, args.timeout) else 1)


if __name__ == "__main__":
    main()
