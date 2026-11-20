#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Raw-RPC callTracer comparison against a pinned, real geth on a private Osaka chain.

Example (each invocation owns only the processes/datadirs it creates):
  python3 tests/l1_call_tracer_parity.py --geth /tmp/n12-geth --prepare --out /tmp/n12-oracle
  python3 tests/l1_call_tracer_parity.py --corpus /tmp/n12-oracle --out /tmp/n12-parity \
    --binary build/fisco-bcos-air/fisco-bcos --tool build/tools/eth-sync-check/eth-sync-check

No mock RPC server, release FISCO binary, proxy, solc, Python dependency, or running
deployment is used. JSONL files retain exact request and response text. The result
comparison preserves every field, omission, child order, byte string and quantity.
"""
import argparse
import base64
import concurrent.futures
import hashlib
import hmac
import json
from pathlib import Path
import re
import socket
import statistics
import subprocess
import threading
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/l1_call_tracer"
ZERO = "0x" + "00" * 32
GETH_COMMIT = "89c1c16a46ce6390d53f8d17cd56caca9c2b15ac"


def dump(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def command(args, cwd=None):
    try:
        return subprocess.check_output(list(map(str, args)), cwd=cwd, stderr=subprocess.STDOUT, text=True)
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Command failed: {args!r}\n{exc.output[-8000:]}") from exc


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class Rpc:
    def __init__(self, url, log, secret=None):
        self.url, self.log, self.secret = url, Path(log), secret
        self.lock = threading.Lock()
        self.sequence = 0

    def raw(self, method, params=(), timeout=40):
        with self.lock:
            self.sequence += 1
            seq = self.sequence
        request = json.dumps({"jsonrpc": "2.0", "id": seq, "method": method, "params": list(params)})
        headers = {"Content-Type": "application/json"}
        if self.secret:
            def b64(data):
                return base64.urlsafe_b64encode(data).rstrip(b"=")
            header = b64(b'{"alg":"HS256","typ":"JWT"}')
            body = b64(json.dumps({"iat": int(time.time())}).encode())
            signature = b64(hmac.new(self.secret, header + b"." + body, hashlib.sha256).digest())
            headers["Authorization"] = "Bearer " + (header + b"." + body + b"." + signature).decode()
        start = time.monotonic()
        try:
            with urllib.request.urlopen(urllib.request.Request(self.url, request.encode(), headers), timeout=timeout) as response:
                raw_response = response.read().decode()
            data = json.loads(raw_response)
            entry = {"url": self.url, "request": request, "response": raw_response,
                     "elapsedSeconds": time.monotonic() - start}
        except Exception as exc:
            entry = {"url": self.url, "request": request, "transportError": str(exc),
                     "elapsedSeconds": time.monotonic() - start}
            with self.lock, self.log.open("a") as output:
                output.write(json.dumps(entry) + "\n")
            raise
        with self.lock, self.log.open("a") as output:
            output.write(json.dumps(entry) + "\n")
        if data.get("jsonrpc") != "2.0" or data.get("id") != seq:
            raise AssertionError(f"Invalid RPC envelope: {data}")
        return data

    def result(self, method, params=(), timeout=40):
        response = self.raw(method, params, timeout)
        if "error" in response:
            raise RuntimeError(f"{method}: {response['error']}")
        return response["result"]


class Processes:
    def __init__(self):
        self.children = []

    def start(self, args, cwd, output):
        log = Path(output).open("w")
        child = subprocess.Popen(list(map(str, args)), cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        self.children.append((child, log))
        return child

    def close(self):
        for child, _ in reversed(self.children):
            if child.poll() is None:
                child.terminate()
        for child, log in reversed(self.children):
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=10)
            log.close()


def ready(child, rpc):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if child.poll() is not None:
            raise RuntimeError(f"Owned node exited with {child.returncode}; inspect its process log")
        try:
            rpc.result("eth_chainId", timeout=1)
            return
        except (OSError, ValueError, RuntimeError):
            time.sleep(0.1)
    raise TimeoutError("Owned node RPC did not become ready")


def choice(block_hash):
    return {"headBlockHash": block_hash, "safeBlockHash": ZERO, "finalizedBlockHash": ZERO}


def require_valid(response):
    assert response.get("payloadStatus", response)["status"] == "VALID", response


def prepare(geth, out, processes):
    version = command([geth, "version"])
    if GETH_COMMIT not in version:
        raise RuntimeError(f"Reference must be pinned to {GETH_COMMIT}: {version}")
    work = out / "geth"
    work.mkdir()
    genesis = FIXTURES / "genesis.json"
    (work / "jwt.hex").write_text("a1" * 32)
    command([geth, "--datadir", work / "data", "--state.scheme", "hash", "init", genesis])
    web_port, engine_port = port(), port()
    child = processes.start([geth, "--datadir", work / "data", "--state.scheme", "hash",
        "--gcmode", "archive", "--http", "--http.addr", "127.0.0.1", "--http.port", web_port,
        "--http.api", "eth,net,web3,debug,txpool", "--authrpc.addr", "127.0.0.1",
        "--authrpc.port", engine_port, "--authrpc.jwtsecret", work / "jwt.hex",
        "--nodiscover", "--maxpeers", "0", "--port", "0", "--ipcdisable", "--syncmode", "full"],
        work, out / "geth-process.log")
    web = Rpc(f"http://127.0.0.1:{web_port}", out / "geth-web3.jsonl")
    engine = Rpc(f"http://127.0.0.1:{engine_port}", out / "geth-engine.jsonl", bytes.fromhex("a1" * 32))
    ready(child, web)
    transactions = json.loads((FIXTURES / "transactions.json").read_text())["transactions"]
    genesis_block = web.result("eth_getBlockByNumber", ["0x0", False])
    head = genesis_block["hash"]
    blocks = []
    for number in range(1, 5):
        expected = [tx for tx in transactions if tx["block"] == number]
        for tx in expected:
            assert web.result("eth_sendRawTransaction", [tx["raw"]]) == tx["hash"]
        root = "0x" + format(number, "02x") * 32
        attrs = {"timestamp": hex(1 + number * 12), "prevRandao": "0x" + "22" * 32,
                 "suggestedFeeRecipient": "0x" + "00" * 19 + "01", "withdrawals": [],
                 "parentBeaconBlockRoot": root}
        fcu = engine.result("engine_forkchoiceUpdatedV3", [choice(head), attrs])
        require_valid(fcu)
        # KZG initializes lazily and can outlast an arbitrary short sleep under
        # concurrent compilation. Wait for the owned builder to finish; the raw
        # payload below remains the acceptance oracle, including exact tx order.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            lines = (out / "geth-process.log").read_text().splitlines()
            if any("Updated payload" in line and f"id={fcu['payloadId']}" in line
                   and re.search(r"\btxs=" + str(len(expected)) + r"\b", line) for line in lines):
                break
            if child.poll() is not None:
                raise RuntimeError("Reference node exited during payload build")
            time.sleep(0.05)
        envelope = engine.result("engine_getPayloadV5", [fcu["payloadId"]])
        payload = envelope["executionPayload"]
        assert payload["transactions"] == [tx["raw"] for tx in expected], "Reference block omitted/reordered transactions"
        requests = envelope.get("executionRequests", [])
        require_valid(engine.result("engine_newPayloadV4", [payload, [], root, requests]))
        head = payload["blockHash"]
        require_valid(engine.result("engine_forkchoiceUpdatedV3", [choice(head), None]))
        raw_block = web.result("debug_getRawBlock", [hex(number)])
        blocks.append({"payload": payload, "parentBeaconBlockRoot": root, "executionRequests": requests,
                       "rlp": raw_block})
    traces = {}
    for tx in transactions:
        receipt = web.result("eth_getTransactionReceipt", [tx["hash"]])
        assert receipt is not None
        if tx["name"] == "output_limit":
            continue  # Tested as an explicit resource error, independently of oracle tree parity.
        traces[tx["name"]] = {}
        for top in (False, True):
            config = {"tracer": "callTracer", "timeout": "30s", "tracerConfig": {"onlyTopCall": top}}
            traces[tx["name"]][str(top)] = web.result("debug_traceTransaction", [tx["hash"], config])
    assert traces["same_block_reader"]["False"]["output"] == "0x" + format(42, "064x")
    assert traces["block_system_call_reader"]["False"]["output"] == "0x" + "01" * 32
    output_hash = next(tx["hash"] for tx in transactions if tx["name"] == "output_limit")
    resource_tree = web.result("debug_traceTransaction", [output_hash, {"tracer": "callTracer", "timeout": "30s"}])
    assert len(resource_tree["calls"]) == 6 and all(len(child["output"]) == 1600002 for child in resource_tree["calls"])
    output_top = web.result("debug_traceTransaction", [output_hash,
        {"tracer": "callTracer", "timeout": "30s", "tracerConfig": {"onlyTopCall": True}}])
    dump(out / "output-limit-tree.json", resource_tree)
    (out / "chain.rlp").write_bytes(b"".join(bytes.fromhex(block["rlp"][2:]) for block in blocks))
    dump(out / "corpus.json", {"referenceCommit": GETH_COMMIT, "referenceVersion": version,
        "referenceBinarySha256": digest(geth), "genesisHash": genesis_block["hash"], "fork": "Osaka",
        "genesisSha256": digest(genesis), "transactionsSha256": digest(FIXTURES / "transactions.json"),
        "blocks": blocks, "transactions": transactions, "traces": traces,
        "outputLimitTop": output_top, "outputLimitTreeSha256": digest(out / "output-limit-tree.json"),
        "chainRlpSha256": digest(out / "chain.rlp")})
    print(f"Prepared {len(blocks)} reference blocks and {len(traces) * 2} raw callTracer oracles in {out}", flush=True)


def node_config(tool, binary, out, processes, chain, window=256):
    work = out / "fisco"
    work.mkdir()
    (work / "conf/engine").mkdir(parents=True)
    (work / "conf/engine/jwt.hex").write_text("a1" * 32)
    command([tool, "--genesis2ini", FIXTURES / "genesis.json", "--output", work / "config.genesis"])
    command(["openssl", "ecparam", "-genkey", "-name", "secp256k1", "-out", work / "conf/node.pem"])
    public = subprocess.check_output(["openssl", "ec", "-in", str(work / "conf/node.pem"), "-pubout", "-outform", "DER"], stderr=subprocess.DEVNULL)
    (work / "conf/node.nodeid").write_text(public[-64:].hex())
    command(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", work / "conf/ca.key",
             "-out", work / "conf/ca.crt", "-days", "1", "-subj", "/CN=call-tracer-test"])
    command(["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", work / "conf/ssl.key",
             "-out", work / "conf/ssl.csr", "-subj", "/CN=call-tracer-node"])
    command(["openssl", "x509", "-req", "-in", work / "conf/ssl.csr", "-CA", work / "conf/ca.crt", "-CAkey", work / "conf/ca.key",
             "-CAcreateserial", "-out", work / "conf/ssl.crt", "-days", "1"])
    (work / "node.rlpx.key").write_text("11" * 32)
    (work / "bootnodes.json").write_text("[]")
    (work / "nodes.json").write_text('{"nodes":[]}')
    web_port, engine_port, rpc_port, p2p_port = port(), port(), port(), port()
    config = (ROOT / "tools/hive/clients/bcos/config.ini.tpl").read_text()
    for old, new in [("__LOG_LEVEL__", "info"), ("listen_port=8545", f"listen_port={web_port}"),
                     ("listen_port=8551", f"listen_port={engine_port}"), ("listen_port=20200", f"listen_port={rpc_port}"),
                     ("listen_port=30300", f"listen_port={p2p_port}"), ("listen_ip=0.0.0.0", "listen_ip=127.0.0.1")]:
        config = config.replace(old, new)
    config = config.replace("[web3_rpc]", "[web3_rpc]\n    enable_trace_api=true")
    config = config.replace("[engine_rpc]", "[engine_rpc]\n    enable_trace_api=true")
    config = config.replace("[storage]", f"[storage]\n    mpt_prune_window={window}")
    config = config.replace("reorg_window=256", f"reorg_window={window}")
    (work / "config.ini").write_text(config)
    # Import canonical geth RLP through the production block verifier.
    # Engine payload ingestion is outside this harness coverage; the current
    # upstream L1 V4 decoder accepts withdrawalsRoot as an optional field.
    with (out / "fisco-import.log").open("w") as log:
        imported = subprocess.run([str(binary), "-c", "config.ini", "-g", "config.genesis",
            "--import-blocks", str(chain)], cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    if imported.returncode:
        raise RuntimeError(f"Canonical block import failed: inspect {out / 'fisco-import.log'}")
    child = processes.start([binary, "-c", "config.ini", "-g", "config.genesis"], work, out / "fisco-process.log")
    web = Rpc(f"http://127.0.0.1:{web_port}", out / "fisco-web3.jsonl")
    engine = Rpc(f"http://127.0.0.1:{engine_port}", out / "fisco-engine.jsonl", bytes.fromhex("a1" * 32))
    ready(child, web)
    return web, engine


def snapshot(web, sender):
    block = web.result("eth_getBlockByNumber", ["latest", False])
    result = {"head": block, "nonce": web.result("eth_getTransactionCount", [sender, "latest"]),
              "pendingNonce": web.result("eth_getTransactionCount", [sender, "pending"]),
              "pendingBlock": web.result("eth_getBlockByNumber", ["pending", False]),
              "balance": web.result("eth_getBalance", [sender, "latest"]),
              "storage": web.result("eth_getStorageAt", ["0x" + format(0x1001, "040x"), "0x0", "latest"])}
    pool = web.raw("txpool_content")
    result["poolObservation"] = {"method": "txpool_content", "available": "result" in pool,
        "result": pool.get("result"), "error": pool.get("error")}
    return result


def differences(expected, actual, path="$", limit=20):
    if type(expected) is not type(actual):
        return [f"{path}: {type(expected).__name__} != {type(actual).__name__}"]
    if isinstance(expected, dict):
        result = []
        for key in sorted(expected.keys() | actual.keys()):
            if key not in expected or key not in actual:
                result.append(f"{path}.{key}: {'unexpected' if key not in expected else 'missing'} field")
            else:
                result.extend(differences(expected[key], actual[key], path + "." + key, limit))
        return result[:limit]
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return [f"{path}: {len(expected)} != {len(actual)} children"]
        return sum((differences(a, b, f"{path}[{i}]", limit) for i, (a, b) in enumerate(zip(expected, actual))), [])[:limit]
    return [] if expected == actual else [f"{path}: {str(expected)[:160]} != {str(actual)[:160]}"]


def compare(corpus, binary, tool, out, processes):
    data = json.loads((corpus / "corpus.json").read_text())
    assert data["genesisSha256"] == digest(FIXTURES / "genesis.json")
    assert data["transactionsSha256"] == digest(FIXTURES / "transactions.json")
    assert data["chainRlpSha256"] == digest(corpus / "chain.rlp")
    web, engine = node_config(tool, binary, out, processes, corpus / "chain.rlp")
    chain_id = int(web.result("eth_chainId"), 16)
    assert chain_id == 133712
    assert web.result("eth_getBlockByNumber", ["0x0", False])["hash"] == data["genesisHash"]
    for block in data["blocks"]:
        payload = block["payload"]
        actual = web.result("eth_getBlockByNumber", [payload["blockNumber"], False])
        assert actual["hash"] == payload["blockHash"]
        for field in ("stateRoot", "receiptsRoot", "gasUsed"):
            assert actual[field] == payload[field], (field, actual[field], payload[field])
    sender = json.loads((FIXTURES / "transactions.json").read_text())["sender"]
    before = snapshot(web, sender)
    results = []
    by_name = {tx["name"]: tx for tx in data["transactions"]}
    for name, oracle in data["traces"].items():
        for top in (False, True):
            options = {"tracer": "callTracer", "timeout": "30s", "tracerConfig": {"onlyTopCall": top}}
            response = web.raw("debug_traceTransaction", [by_name[name]["hash"], options])
            mismatch = differences(oracle[str(top)], response.get("result"))
            results.append({"case": name, "onlyTopCall": top, "pass": not mismatch, "differences": mismatch})
            print(f"{'PASS' if not mismatch else 'FAIL'} {name} onlyTopCall={top}" + (": " + "; ".join(mismatch[:3]) if mismatch else ""), flush=True)
    good_hash = by_name["same_block_reader"]["hash"]
    # Every rejection is followed by a legal trace AND a node-alive probe.
    negative = [
        ("unknown_transaction", [ZERO, {"tracer": "callTracer"}]),
        ("unsupported_tracer", [good_hash, {"tracer": "prestateTracer"}]),
        ("missing_tracer", [good_hash, {}]),
        ("invalid_timeout", [good_hash, {"tracer": "callTracer", "timeout": "bananas"}]),
        ("excessive_timeout", [good_hash, {"tracer": "callTracer", "timeout": "31s"}]),
        ("non_boolean_top", [good_hash, {"tracer": "callTracer", "tracerConfig": {"onlyTopCall": "true"}}]),
        ("timeout", [by_name["timeout_loop"]["hash"], {"tracer": "callTracer", "timeout": "1ms"}]),
        ("output_limit", [by_name["output_limit"]["hash"], {"tracer": "callTracer", "timeout": "30s"}]),
    ]
    parameter_errors = {"unsupported_tracer", "missing_tracer", "invalid_timeout", "excessive_timeout", "non_boolean_top"}
    for name, params in negative:
        started = time.monotonic()
        response = web.raw("debug_traceTransaction", params, timeout=35)
        elapsed = time.monotonic() - started
        positive = web.result("debug_traceTransaction", [good_hash, {"tracer": "callTracer"}])
        alive = web.result("eth_blockNumber")
        error = response.get("error", {})
        expected_code = -32602 if name in parameter_errors else -32000
        ok = error.get("code") == expected_code and positive == data["traces"]["same_block_reader"]["False"] and alive == "0x4"
        if name == "timeout":
            ok = ok and elapsed < 2 and error.get("message") == "execution timeout"
        if name == "output_limit":
            ok = ok and "budget" in error.get("message", "").lower()
        if name == "unknown_transaction":
            ok = ok and error.get("message") == "transaction not found"
        results.append({"case": name, "pass": ok, "response": response, "elapsedSeconds": elapsed})
        print(f"{'PASS' if ok else 'FAIL'} {name}: {response.get('error', 'unexpected result')}", flush=True)
    # Overlapping cancellation requests must leave capacity for the next normal trace.
    barrier = threading.Barrier(8)
    def concurrent_trace(_):
        barrier.wait(timeout=10)
        return web.raw("debug_traceTransaction", [by_name["timeout_loop"]["hash"], {"tracer": "callTracer", "timeout": "200ms"}])
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:
        concurrent_results = list(workers.map(concurrent_trace, range(8)))
    recovered = web.result("debug_traceTransaction", [good_hash, {"tracer": "callTracer"}])
    limits = [r for r in concurrent_results if "concurrency limit" in r.get("error", {}).get("message", "").lower()]
    valid_concurrent = all((r.get("error", {}).get("code") == -32000 and
        any(word in r["error"]["message"].lower() for word in ("concurrency limit", "timed out", "timeout")))
        or r.get("result") == data["traces"]["timeout_loop"]["False"] for r in concurrent_results)
    results.append({"case": "concurrent_slot_limit_and_recovery", "pass": bool(limits) and valid_concurrent
                    and recovered == data["traces"]["same_block_reader"]["False"], "responses": concurrent_results})
    after = snapshot(web, sender)
    results.append({"case": "trace_preserves_canonical_chain_accounts_and_pending_rpc", "pass": before == after,
                    "differences": differences(before, after)})
    # Challenger preimage APIs stay disabled in a bounded, non-archive trace profile.
    preimage = web.raw("debug_getRawHeader", ["latest"])
    results.append({"case": "trace_independent_of_archive_debug", "pass": preimage.get("error", {}).get("code") == -32601,
                    "response": preimage})
    engine_trace = engine.result("debug_traceTransaction", [good_hash, {"tracer": "callTracer"}])
    results.append({"case": "authenticated_engine_trace", "pass": engine_trace == data["traces"]["same_block_reader"]["False"]})
    output_top = web.result("debug_traceTransaction", [by_name["output_limit"]["hash"],
        {"tracer": "callTracer", "tracerConfig": {"onlyTopCall": True}, "timeout": "30s"}])
    results.append({"case": "only_top_call_avoids_suppressed_child_output_budget", "pass": output_top == data["outputLimitTop"]})
    # Use a separate bounded-window chain, retaining bodies/receipts while pruning old state.
    pruned_out = out / "pruned-profile"
    pruned_out.mkdir()
    pruned_web, pruned_engine = node_config(tool, binary, pruned_out, processes, corpus / "chain.rlp", window=1)
    # Prove the transaction still exists and that only its replay data is unavailable.
    old_receipt = pruned_web.result("eth_getTransactionReceipt", [good_hash])
    missing_state = pruned_web.raw("debug_traceTransaction", [good_hash, {"tracer": "callTracer"}])
    current_trace = pruned_web.raw("debug_traceTransaction", [by_name["output_limit"]["hash"],
        {"tracer": "callTracer", "tracerConfig": {"onlyTopCall": True}, "timeout": "30s"}])
    results.append({"case": "pruned_parent_state_is_explicit", "pass": old_receipt is not None
        and missing_state.get("error", {}).get("code") == -32004 and current_trace.get("result") == data["outputLimitTop"]
        and pruned_web.result("eth_blockNumber") == "0x4", "response": missing_state,
        "currentPositiveControl": current_trace, "oldReceipt": old_receipt})
    source_paths = set(command(["git", "diff", "--name-only", "HEAD"], ROOT).splitlines())
    added_prefixes = ("bcos-codec/bcos-codec/abi/", "bcos-framework/bcos-framework/dispatcher/",
        "ethereum-executor/", "ports/evmone/", "transaction-scheduler/tests/",
        "tests/l1_call_tracer", "tests/fixtures/l1_call_tracer/")
    source_paths.update(path for path in command(["git", "ls-files", "--others", "--exclude-standard"], ROOT).splitlines()
        if path.startswith(added_prefixes) and Path(path).suffix in (".cpp", ".h", ".patch", ".py", ".json"))
    source_paths.update(["ethereum-executor/EthereumCallTracer.h", "bcos-codec/bcos-codec/abi/RevertReason.h",
        "bcos-framework/bcos-framework/dispatcher/TransactionTrace.h", "ports/evmone/trace-cancellation.patch",
        "transaction-scheduler/tests/TestTransactionCallTrace.cpp",
        "tests/l1_call_tracer_parity.py", "tests/fixtures/l1_call_tracer/generate.py",
        "tests/fixtures/l1_call_tracer/genesis.json", "tests/fixtures/l1_call_tracer/transactions.json"])
    source_manifest = {path: digest(ROOT / path) for path in sorted(source_paths) if (ROOT / path).is_file()}
    build_dir = binary.parent.parent
    dependency_paths = [build_dir / "vcpkg_installed/arm64-osx/include/evmone/tracing.hpp",
        build_dir / "vcpkg_installed/arm64-osx/include/evmone/execution_state.hpp",
        build_dir / "vcpkg_installed/arm64-osx/debug/lib/libevmone.a",
        build_dir / "vcpkg_installed/arm64-osx/debug/lib/libevmone_precompiles.a"]
    dependency_manifest = {str(path): digest(path) for path in dependency_paths if path.is_file()}
    build_options = {}
    if (build_dir / "CMakeCache.txt").is_file():
        for line in (build_dir / "CMakeCache.txt").read_text().splitlines():
            key = line.split(":", 1)[0]
            if key in ("CMAKE_CXX_COMPILER", "CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "CMAKE_OSX_SYSROOT", "VCPKG_TARGET_TRIPLET", "CMAKE_PROJECT_INCLUDE"):
                build_options[key] = line.split("=", 1)[1]
    genesis_config = json.loads((FIXTURES / "genesis.json").read_text())["config"]
    native_timings = []
    for line in web.log.read_text().splitlines():
        entry = json.loads(line)
        request = json.loads(entry["request"])
        tx_hash = request["params"][0] if request["params"] else None
        name = next((tx["name"] for tx in data["transactions"] if tx["hash"] == tx_hash), "")
        if request["method"] == "debug_traceTransaction" and name.startswith(("kzg_", "bn_", "bls_", "blake_", "modexp_")):
            native_timings.append({"case": name, "elapsedSeconds": entry["elapsedSeconds"],
                "onlyTopCall": request["params"][1].get("tracerConfig", {}).get("onlyTopCall", False)})
    report = {"lane": "Eth", "executorVersion": 2, "chainId": chain_id, "fork": "Osaka",
              "blockIngress": "Unmodified canonical geth RLP through fisco-bcos --import-blocks",
              "enginePayloadCoverage": "Not exercised: this harness uses production RLP block import; upstream L1 V4 accepts optional withdrawalsRoot",
              "forkTimestamps": {key: value for key, value in genesis_config.items() if key.endswith("Time")},
              "interfaceBaseline": {"debug_traceTransaction": "geth callTracer at " + data["referenceCommit"],
                  "engine": "Osaka: forkchoiceUpdatedV3/newPayloadV4/getPayloadV5"},
              "referenceCommit": data["referenceCommit"], "referenceVersion": data["referenceVersion"],
              "referenceBinarySha256": data["referenceBinarySha256"], "nodeBinary": str(binary),
              "nodeBinarySha256": digest(binary), "nodeCommit": command(["git", "rev-parse", "HEAD"], ROOT).strip(),
              "nodeDiffSha256": hashlib.sha256(command(["git", "diff", "HEAD"], ROOT).encode()).hexdigest(),
              "sourceManifest": source_manifest, "dependencyManifest": dependency_manifest, "buildOptions": build_options,
              "nativePrecompileWholeRpcTimings": {"samples": native_timings,
                  "maxSeconds": max(row["elapsedSeconds"] for row in native_timings),
                  "medianSeconds": statistics.median(row["elapsedSeconds"] for row in native_timings),
                  "scope": "Whole RPC including prefix replay and overhead; not a worst-case native duration or hard deadline guarantee"},
              "poolObservability": before["poolObservation"],
              "unsupportedChecks": [{"check": "Live Ethereum MemPoolImpl snapshot", "status": "unsupported",
                  "reason": "txpool_content is Op-gated; native/pending RPC reads a separate legacy pool; no pool snapshot counted as a pass"}],
              "comparison": "Every JSON value, omitted field, byte string and ordered child frame must match; JSON object key order is ignored",
              "genesisSha256": data["genesisSha256"], "results": results, "before": before, "after": after}
    dump(out / "report.json", report)
    assert all(row["pass"] for row in results), f"Parity failed; inspect {out / 'report.json'}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--geth", type=Path)
    ap.add_argument("--prepare", action="store_true")
    ap.add_argument("--corpus", type=Path)
    ap.add_argument("--binary", type=Path, default=ROOT / "build/fisco-bcos-air/fisco-bcos")
    ap.add_argument("--tool", type=Path, default=ROOT / "build/tools/eth-sync-check/eth-sync-check")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    processes = Processes()
    try:
        if args.prepare:
            if not args.geth:
                ap.error("--prepare requires --geth")
            prepare(args.geth.resolve(), out, processes)
        else:
            if not args.corpus:
                ap.error("comparison requires --corpus from --prepare")
            compare(args.corpus.resolve(), args.binary.resolve(), args.tool.resolve(), out, processes)
    except Exception as exc:
        dump(out / "failure.json", {"error": str(exc), "type": type(exc).__name__})
        raise
    finally:
        processes.close()


if __name__ == "__main__":
    main()
