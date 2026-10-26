#!/usr/bin/env python3
"""End-to-end exactness test for eth_estimateGas.

What "exact" means here
-----------------------
For a call, let EG be what eth_estimateGas returns. The estimate is *exact* iff a transaction
sent with gasLimit = EG succeeds and one sent with gasLimit = EG - 1 runs out of gas. That is
the only definition that matters to a client: EG must be usable, and must not waste headroom.
Two other outcomes are reported as defects:

    UNDER   EG itself fails          -- the node returns a limit its own admission accepts but
                                        the EVM cannot execute: the caller's transaction dies.
    OVER    EG-1 also succeeds       -- safe but imprecise; the gap is measured and printed.

The test runs against a live node, because the property involves the whole path
(eth_estimateGas -> scheduler -> executor -> evmone) and not just the accumulator.

Method
------
1. `eth_estimateGas` for the case -> EG.
2. Send a real transaction with gasLimit = EG       -> must succeed.
3. Send a real transaction with gasLimit = EG - 1   -> must fail.
4. Only if step 3 succeeded (an over-estimate), binary-search the real minimum so the gap is
   a number rather than "not exact".

Why the harness contracts are idempotent
----------------------------------------
Step 2 runs before step 3, so it must not change anything the later runs depend on. Every
storage write in GasHarness.sol stores the value already present, so each SSTORE costs the
same on every run and the estimate stays valid across steps.

Usage
-----
    python3 run.py --rpc http://127.0.0.1:8545 --key 0x...
    python3 run.py --rpc ... --key ... --harness 0x...      # reuse a deployed harness
    python3 run.py --rpc ... --key ... --no-search           # skip the binary search

Requirements: `forge` on PATH (contracts are compiled in a temporary project), and a node with
eth_call served through ethereum-executor (baseline_scheduler=true on the v2 lane) -- without
that the estimate silently degrades to the raw gasUsed and every SSTORE case shows up as an
UNDER.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

GAS_PRICE = "1000000000"


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


class Rpc:
    def __init__(self, url):
        self.url = url
        self.id = 0

    def call(self, method, params):
        self.id += 1
        body = json.dumps(
            {"jsonrpc": "2.0", "id": self.id, "method": method, "params": params}
        ).encode()
        req = urllib.request.Request(
            self.url, body, {"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=120) as r:
            return json.load(r)

    def result(self, method, params):
        resp = self.call(method, params)
        if "error" in resp:
            raise RuntimeError(f"{method}: {resp['error'].get('message')}")
        return resp["result"]

    def estimate_gas(self, frm, to, data):
        resp = self.call(
            "eth_estimateGas", [{"from": frm, "to": to, "data": data, "value": "0x0"}]
        )
        if "error" in resp:
            return None, resp["error"].get("message", "unknown")
        return int(resp["result"], 16), None


def compile_harness(contracts_dir, workdir):
    """Compile the harness in a throwaway forge project, leaving the repo free of build output."""
    src = os.path.join(workdir, "src")
    os.makedirs(src, exist_ok=True)
    for name in os.listdir(contracts_dir):
        if name.endswith(".sol"):
            shutil.copy(os.path.join(contracts_dir, name), src)
    with open(os.path.join(workdir, "foundry.toml"), "w") as f:
        f.write('[profile.default]\nsrc = "src"\nout = "out"\noptimizer = false\n')
    proc = subprocess.run(
        ["forge", "build"], cwd=workdir, capture_output=True, text=True, timeout=600
    )
    if proc.returncode != 0:
        die(f"forge build failed:\n{proc.stdout}\n{proc.stderr}")
    artifact = os.path.join(workdir, "out", "GasHarness.sol", "GasHarness.json")
    with open(artifact) as f:
        return json.load(f)["bytecode"]["object"]


def compile_artifact(workdir, name):
    artifact = os.path.join(workdir, "out", "GasHarness.sol", name + ".json")
    with open(artifact) as f:
        return json.load(f)["bytecode"]["object"]


def send(rpc_url, key, to, data, gas_limit, value=0):
    """Send one legacy transaction and return (status, gasUsed)."""
    cmd = [
        "cast", "send",
        "--rpc-url", rpc_url,
        "--private-key", key,
        "--legacy",
        "--gas-price", GAS_PRICE,
        "--gas-limit", str(gas_limit),
    ]
    if value:
        cmd += ["--value", str(value)]
    cmd += [to, data]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if proc.returncode != 0:
        raise RuntimeError((proc.stderr or proc.stdout).strip()[:300])
    status = gas_used = None
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "status":
            status = int(parts[1]) if parts[1].isdigit() else parts[1]
        elif len(parts) >= 2 and parts[0] == "gasUsed":
            gas_used = int(parts[1])
    return status, gas_used


def succeeds(rpc_url, key, to, data, gas_limit, value=0):
    status, _ = send(rpc_url, key, to, data, gas_limit, value)
    return status == 1




def true_minimum(rpc_url, key, to, data, upper, lower_hint=0, value=0):
    """Least gasLimit that still succeeds, by exponential-then-binary search below @p upper."""
    # upper is known to succeed; walk down until something fails.
    lo = max(lower_hint, 21000)
    step = max(upper // 8, 1000)
    while lo < upper:
        probe = max(upper - step, lo)
        if succeeds(rpc_url, key, to, data, probe, value):
            upper = probe
            step *= 2
        else:
            lo = probe
            break
    while upper - lo > 1:
        mid = (lo + upper) // 2
        if succeeds(rpc_url, key, to, data, mid, value):
            upper = mid
        else:
            lo = mid
    return upper


def swallow_calldata(payload_bytes):
    """Calldata for swallow(bytes) with a payload of `payload_bytes` bytes of 0xab."""
    payload = "ab" * payload_bytes
    out = subprocess.run(
        ["cast", "calldata", "swallow(bytes)", "0x" + payload],
        capture_output=True, text=True, timeout=60,
    )
    if out.returncode != 0:
        die(f"cast calldata failed: {out.stderr.strip()}")
    return out.stdout.strip()


CASES = [
    # (label, function, args)
    ("flat/no-store", "rec", [0, 200, 0]),
    ("flat/store", "rec", [0, 200, 1]),
    ("rec2/no-store", "rec", [2, 100, 0]),
    ("rec4/no-store", "rec", [4, 60, 0]),
    ("rec8/no-store", "rec", [8, 25, 0]),
    ("rec4/store-every", "rec", [4, 60, 1]),
    ("rec8/store-every", "rec", [8, 25, 1]),
    ("rec4/store-leaf-only", "rec", [4, 60, 2]),
    ("rec8/store-leaf-only", "rec", [8, 25, 2]),
    ("rec4/store-after", "rec", [4, 60, 3]),
    ("fan3/no-store", "fan", [3, 60, 0]),
    ("fan3/store-every", "fan", [3, 60, 1]),
    ("rec3/two-slots", "twoSlots", [3, 60]),
    # EIP-7623 floor: 8 KiB of calldata, ~26k of intrinsic, almost no execution. Without the
    # floor in the answer the estimate lands below max(intrinsic, min_cost) and this case is
    # UNDER -- the defect this case exists to catch.
    ("calldata-heavy/floor", "swallow", [("bytes", 8192)]),
]

SIGS = {
    "rec": "rec(uint256,uint256,uint256)",
    "fan": "fan(uint256,uint256,uint256)",
    "twoSlots": "twoSlots(uint256,uint256)",
}


def encode(fn, args):
    if fn == "swallow":
        return swallow_calldata(args[0][1])
    out = subprocess.run(
        ["cast", "calldata", SIGS[fn], *[str(a) for a in args]],
        capture_output=True, text=True, timeout=60,
    )
    if out.returncode != 0:
        die(f"cast calldata failed: {out.stderr.strip()}")
    return out.stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpc", default="http://127.0.0.1:8545")
    ap.add_argument("--key", required=True)
    ap.add_argument("--harness", help="reuse an already deployed GasHarness")
    ap.add_argument("--contracts", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "contracts"))
    ap.add_argument("--no-search", action="store_true", help="skip the true-minimum search")
    ap.add_argument("--strict", action="store_true",
                    help="also require every no-SSTORE case to be EXACT (for CI)")
    args = ap.parse_args()

    rpc = Rpc(args.rpc)
    sender = subprocess.run(
        ["cast", "wallet", "address", "--private-key", args.key],
        capture_output=True, text=True, timeout=60,
    ).stdout.strip()

    workdir = tempfile.mkdtemp(prefix="gas-exactness-")
    try:
        harness = args.harness
        if not harness:
            print("[deploy] compiling and deploying GasHarness ...", flush=True)
            bytecode = compile_harness(args.contracts, workdir)
            proc = subprocess.run(
                ["cast", "send", "--rpc-url", args.rpc, "--private-key", args.key,
                 "--legacy", "--gas-price", GAS_PRICE, "--gas-limit", "3000000",
                 "--json", "--create", bytecode],
                capture_output=True, text=True, timeout=300,
            )
            if proc.returncode != 0:
                die(f"deploy failed: {(proc.stderr or proc.stdout).strip()[:400]}")
            tx = json.loads(proc.stdout)["transactionHash"]
            receipt = rpc.result("eth_getTransactionReceipt", [tx])
            if receipt.get("status") != "0x1":
                die(f"deploy transaction failed: {receipt.get('status')}")
            harness = receipt.get("contractAddress")
            if not harness or harness == "0x0000000000000000000000000000000000000000":
                # This node leaves contractAddress blank in receipts, so derive it from the
                # sender's nonce at deployment time instead of trusting the field.
                nonce = int(rpc.result("eth_getTransactionByHash", [tx])["nonce"], 16)
                out = subprocess.run(
                    ["cast", "compute-address", "--nonce", str(nonce), sender],
                    capture_output=True, text=True, timeout=60,
                )
                harness = out.stdout.strip().split()[-1]
            if not harness:
                die("deploy produced no contract address")
        # The DELEGATECALL case needs its own target: a payable proxy that delegatecalls into
        # the harness, so the inner frame carries msg.value without the 2300 stipend.
        print("[deploy] deploying DelegateProxy ...", flush=True)
        proxy_bytecode = compile_artifact(workdir, "DelegateProxy")
        ctor_args = harness[2:].rjust(64, "0")
        proc = subprocess.run(
            ["cast", "send", "--rpc-url", args.rpc, "--private-key", args.key,
             "--legacy", "--gas-price", GAS_PRICE, "--gas-limit", "3000000",
             "--json", "--create", proxy_bytecode + ctor_args],
            capture_output=True, text=True, timeout=300,
        )
        if proc.returncode != 0:
            die(f"proxy deploy failed: {(proc.stderr or proc.stdout).strip()[:400]}")
        proxy_tx = json.loads(proc.stdout)["transactionHash"]
        proxy_receipt = rpc.result("eth_getTransactionReceipt", [proxy_tx])
        proxy = proxy_receipt.get("contractAddress")
        if not proxy or proxy == "0x0000000000000000000000000000000000000000":
            nonce = int(rpc.result("eth_getTransactionByHash", [proxy_tx])["nonce"], 16)
            proxy = subprocess.run(
                ["cast", "compute-address", "--nonce", str(nonce), sender],
                capture_output=True, text=True, timeout=60,
            ).stdout.strip().split()[-1]
        # Seed the delegated slot: the proxy's slot 0 is empty, so the first write is 0 -> 1
        # (SET) and every later one is 1 -> 1 (cheap). Without this the estimate would see a
        # SET and the transaction that follows a cheap write.
        proxy_call = subprocess.run(
            ["cast", "calldata", "run()"], capture_output=True, text=True, timeout=60
        ).stdout.strip()
        send(args.rpc, args.key, proxy, proxy_call, 500000)

        print(f"[info] sender={sender} harness={harness} proxy={proxy}\n", flush=True)

        rows = []
        all_cases = [(label, fn, fn_args, harness, 0) for label, fn, fn_args in CASES]
        # A value transfer on the outer call: DELEGATECALL inherits msg.value but no stipend.
        all_cases.append(("delegatecall/value", "run", [], proxy, 1))
        for label, fn, fn_args, harness, value in all_cases:
            data = encode(fn, fn_args)
            eg, err = rpc.estimate_gas(sender, harness, data)
            if eg is None:
                rows.append((label, None, "ERROR", err, None))
                print(f"{label:26} estimateGas failed: {err}", flush=True)
                continue

            # The property, directly: EG must run, EG-1 must not.
            ok_at_eg = succeeds(args.rpc, args.key, harness, data, eg, value)
            if not ok_at_eg:
                # A limit this node accepted but the EVM cannot execute: the caller's
                # transaction would die. Never acceptable.
                verdict, delta = "UNDER", None
            else:
                status_1, _ = send(args.rpc, args.key, harness, data, eg - 1, value)
                if status_1 == 1:
                    # EG-1 also runs: safe, but not the minimum.
                    verdict, delta = "OVER", None
                    if not args.no_search:
                        delta = eg - true_minimum(
                            args.rpc, args.key, harness, data, eg - 1, value=value
                        )
                else:
                    # EG-1 failed. Note the failed transaction need not consume its whole
                    # limit: a child that runs out of gas makes the parent's
                    # `require(ok)` revert, so the reported gasUsed stays below the limit.
                    # The EVM is deterministic, so failure at EG-1 together with success at
                    # EG is exactly the minimality property.
                    verdict, delta = "EXACT", 0

            rows.append((label, eg, verdict, delta, None))
            extra = "" if delta is None else f"  (真实最小 {eg - delta}, 余量 {delta})"
            print(f"{label:26} EG={eg:<10} {verdict}{extra}", flush=True)

        print("\n===== 汇总 =====")
        print(f"{'case':26} {'EG':>10}  {'结果':6} {'余量':>8}")
        for label, eg, verdict, delta, err in rows:
            d = "" if delta is None else str(delta)
            print(f"{label:26} {str(eg):>10}  {verdict:6} {d:>8}")

        under = [r for r in rows if r[2] == "UNDER"]
        errored = [r for r in rows if r[2] == "ERROR"]
        over = [r for r in rows if r[2] == "OVER"]

        if under:
            print(f"\nFAIL: {len(under)} 个 UNDER —— 估算值自己跑不过，客户端交易会失败。")
            return 1
        if errored:
            print(f"\nFAIL: {len(errored)} 个用例 estimateGas 直接报错。")
            return 1
        if over:
            print(f"\n{len(over)} 个用例 OVER（安全，但有余量）。")

        # No under-estimate anywhere is the invariant that must hold. Beyond that, --strict
        # also demands that the no-SSTORE cases be exact: those exercise only EIP-150, which
        # the accumulator models exactly, so a margin there would be a real defect.
        if args.strict:
            not_exact = [r for r in rows if "/no-store" in r[0] and r[2] != "EXACT"]
            if not_exact:
                print(f"\nFAIL(--strict): {len(not_exact)} 个无 SSTORE 用例不精确："
                      f"{[r[0] for r in not_exact]}")
                return 1
            print("OK(--strict): 无 UNDER，且所有无 SSTORE 用例均为 EXACT。")
        else:
            print("OK: 无 UNDER（估算值永远不低于实际需要）。")
        return 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
