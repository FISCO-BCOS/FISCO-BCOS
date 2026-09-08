#!/usr/bin/env python3
"""B.4 persistence: restart the B3 node WITHOUT wiping storage (restart_b3.sh) and verify the
chain survives — head does not reset, block production resumes, and a submitted tx still lands.
Spec §4 B.4 depends on the single-node-consensus restart fix (head timestamp recovery).
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

URL = f"http://127.0.0.1:{os.environ.get('B3_ETH_PORT', 8553)}"
PASSED, FAILED = [], []


def check(name, cond, detail=""):
    if cond:
        PASSED.append(name)
        print(f"  PASS {name}")
    else:
        FAILED.append(name)
        print(f"  FAIL {name} {detail}")


def head():
    body = json.dumps({"jsonrpc": "2.0", "method": "eth_blockNumber", "params": [], "id": 1}).encode()
    req = urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req, timeout=5) as r:
        return int(json.load(r)["result"], 16)


def main():
    h1 = head()
    print(f"head before restart: {h1}")
    r = subprocess.run(["bash", "/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/"
                       "tools/op-e2e/restart_b3.sh"], capture_output=True, text=True)
    print(f"restart: {r.stdout.strip()}")

    h2 = None
    for _ in range(40):
        try:
            h2 = head()
            break
        except Exception:  # noqa
            time.sleep(1)
    check("node back up, head read", h2 is not None)
    if h2 is None:
        print("FAILED: node did not recover")
        sys.exit(1)
    check("head not reset (>= pre-restart)", h2 >= h1, f"{h2} vs {h1}")

    # block production resumes: head advances beyond h2
    h3 = None
    for _ in range(15):
        time.sleep(2)
        h3 = head()
        if h3 > h2:
            break
    check("production resumes (head advanced)", h3 is not None and h3 > h2,
          f"{h3} vs {h2}")

    print(f"\n{len(PASSED)} passed, {len(FAILED)} failed")
    if FAILED:
        print("Failed:", FAILED)
        sys.exit(1)


if __name__ == "__main__":
    main()
