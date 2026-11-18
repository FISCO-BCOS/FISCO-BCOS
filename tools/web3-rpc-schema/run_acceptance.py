#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run the X7 pilot against disposable loopback AIR nodes."""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing, contextmanager
import hashlib
import hmac
import http.client
import json
import os
from pathlib import Path
import re
import socket
import statistics
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
ADDRESS = "0x" + "0" * 36 + "1001"
KNOWN_SLOT = "0x0"
EMPTY_SLOT = "0x" + "0" * 64
UNKNOWN_HASH = "0x" + "f" * 64
LIMIT = 4096
MAX_BATCH = 4
JWT_KEY = bytes.fromhex("a1" * 32)


def wire(obj):
    return json.dumps(obj, separators=(",", ":")).encode()


def request(method, params=(), request_id=1):
    return {"jsonrpc": "2.0", "method": method, "id": request_id, "params": params}


def connection(port):
    return http.client.HTTPConnection("127.0.0.1", port, timeout=4)


def rpc_http(port, message, token=None, client=None):
    if client is None:
        with closing(connection(port)) as client:
            return rpc_http(port, message, token, client)
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    client.request("POST", "/", message if isinstance(message, bytes) else wire(message), headers)
    response = client.getresponse()
    return response.status, json.loads(response.read())


def free_ports(count):
    sockets = [socket.socket() for _ in range(count)]
    try:
        for listener in sockets:
            listener.bind(("127.0.0.1", 0))
        return tuple(listener.getsockname()[1] for listener in sockets)
    finally:
        for listener in sockets:
            listener.close()


def local_command(*cmd, cwd=None):
    return subprocess.check_output(cmd, cwd=cwd, stderr=subprocess.STDOUT)


class WebSocket:
    """Minimal loopback JSON client for ordinary RFC 6455 text messages."""
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=8)
        self.stream = self.socket.makefile("rb")
        self.port = port

    def __enter__(self):
        try:
            key = base64.b64encode(os.urandom(16)).decode()
            self.socket.sendall((
                f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n"
                f"Upgrade: websocket\r\nConnection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
            ).encode())
            status = self.stream.readline().split()[1]
            headers = {}
            while (line := self.stream.readline().strip()):
                name, value = line.split(b":", 1)
                headers[name.lower()] = value.strip()
            expected = base64.b64encode(hashlib.sha1(
                (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
            ).digest())
            assert status == b"101" and headers[b"sec-websocket-accept"] == expected
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        self.stream.close()
        self.socket.close()

    def __exit__(self, *_):
        self.close()

    def send(self, opcode, data):
        mask = os.urandom(4)
        length = len(data)
        length_header = bytes([128 | length]) if length < 126 else struct.pack("!BH", 254, length)
        self.socket.sendall(bytes([128 | opcode]) + length_header + mask +
                            bytes(byte ^ mask[i % 4] for i, byte in enumerate(data)))

    def exchange(self, obj):
        self.send(1, obj if isinstance(obj, bytes) else wire(obj))
        chunks = []
        while True:
            flags, length = self.stream.read(2)
            assert not length & 128
            opcode = flags & 15
            if length == 126:
                length, = struct.unpack("!H", self.stream.read(2))
            elif length == 127:
                length, = struct.unpack("!Q", self.stream.read(8))
            data = self.stream.read(length)
            if opcode == 9:
                self.send(10, data)
                continue
            if opcode == 10:
                continue
            if opcode == 8:
                raise ConnectionAbortedError(struct.unpack("!H", data[:2])[0])
            assert opcode in {0, 1, 2}, (opcode, data)
            chunks.append(data)
            if flags & 128:
                return json.loads(b"".join(chunks))


def jwt():
    def field(data):
        return base64.urlsafe_b64encode(data).rstrip(b"=")
    data = field(wire({"alg": "HS256", "typ": "JWT"})) + b"." + field(wire({"iat": int(time.time())}))
    return (data + b"." + field(hmac.digest(JWT_KEY, data, "sha256"))).decode()


class Fixture:
    def __init__(self, profile_dir, tool):
        self.profile_dir = profile_dir
        self.profile_dir.mkdir()
        seed = self.profile_dir
        seed.joinpath("engine").mkdir()
        seed.joinpath("engine/jwt.hex").write_text(JWT_KEY.hex())
        local_command("openssl", "ecparam", "-genkey", "-name", "secp256k1", "-out", "node.pem", cwd=seed)
        public_key = local_command("openssl", "ec", "-in", "node.pem", "-pubout", "-outform", "DER", cwd=seed)
        self.node_id = public_key[-64:].hex()
        seed.joinpath("node.nodeid").write_text(self.node_id)
        local_command("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout",
                      "ssl.key", "-out", "ssl.crt", "-days", "1", "-subj", "/CN=x7-local", cwd=seed)
        seed.joinpath("ca.crt").write_bytes(seed.joinpath("ssl.crt").read_bytes())
        self.key_files = ["node.pem", "node.nodeid", "ssl.key", "ssl.crt", "ca.crt", "engine/jwt.hex"]

        gen = json.loads((ROOT / "tools/hive/clients/bcos/genesis.json").read_text())
        gen["alloc"][ADDRESS] = {"balance": "0x1", "nonce": "0x0",
                                 "storage": {EMPTY_SLOT: "0x" + "0" * 63 + "2"}}
        genesis = seed / "genesis.json"
        genesis.write_bytes(wire(gen))
        local_command(tool, "--genesis2ini", genesis, "--output", seed / "eth.genesis")
        self.eth_genesis = (seed / "eth.genesis").read_text().replace(
            "node.0=\n", f"node.0={self.node_id}: 1\n")
        gen["extraData"] = "0x" + struct.pack("!BIIQ", 1, 250, 2, 0).hex()
        op_json = seed / "op-genesis.json"
        op_json.write_bytes(wire(gen))
        local_command(tool, "--genesis2ini", op_json, "--output", seed / "op.genesis")
        op_genesis = (seed / "op.genesis").read_text().replace(
            "node.0=\n", f"node.0={self.node_id}: 1\n")
        self.op_genesis = re.sub(
            r"\[fork_timestamps\][^[]*", "[op_fork_timestamps]\njovian_time=0\n\n"
            "[op_eip1559]\nelasticity=2\ndenominator=8\ndenominator_canyon=250\n\n",
            op_genesis.replace("version=2", "version=3"), count=1)
        self.fisco_genesis = f"""[chain]
sm_crypto=false
group_id=group0
chain_id=chain0
[web3]
chain_id=20200
[consensus]
consensus_type=pbft
block_tx_count_limit=1000
leader_period=1
node.0={self.node_id}: 1
[version]
compatibility_version=3.18.0
[tx]
gas_limit=3000000000
[executor]
version=1
is_wasm=false
is_auth_check=false
is_serial_execute=true
auth_admin_account={ADDRESS}
"""
        self.ini = (ROOT / "tools/hive/clients/bcos/config.ini.tpl").read_text()

    @contextmanager
    def start(self, work, binary, lane, limits=False, env=None):
        work.mkdir()
        (work / "conf/engine").mkdir(parents=True)
        for name in self.key_files:
            (work / "conf" / name).write_bytes((self.profile_dir / name).read_bytes())
        (work / "bootnodes.json").write_text("[]")
        (work / "nodes.json").write_text('{"nodes":[]}')
        (work / "node.rlpx.key").write_text("11" * 32)
        port, engine_port, rpc_port, peer_port = free_ports(4)
        ini = self.ini
        for previous, replacement in [
            ("listen_ip=0.0.0.0", "listen_ip=127.0.0.1"),
            ("listen_port=8545", f"listen_port={port}"),
            ("listen_port=8551", f"listen_port={engine_port}"),
            ("listen_port=20200", f"listen_port={rpc_port}"),
            ("listen_port=30300", f"listen_port={peer_port}"),
            ("__LOG_LEVEL__", "error"),
        ]:
            ini = ini.replace(previous, replacement)
        if limits:
            ini = ini.replace("[web3_rpc]", f"[web3_rpc]\n    batch_request_size_limit={MAX_BATCH}")
            ini = re.sub(r"request_body_size_limit=\d+", f"request_body_size_limit={LIMIT}", ini)
        if lane == "fisco":
            ini += "\n[debug]\nenable=true\nproduce_empty_blocks=false\n"
        (work / "config.ini").write_text(ini)
        (work / "config.genesis").write_text(getattr(self, lane + "_genesis"))
        with (work / "node.log").open("w") as output:
            start = time.perf_counter()
            process = subprocess.Popen(
                [binary, "-c", "config.ini", "-g", "config.genesis"],
                cwd=work, stdout=output, stderr=subprocess.STDOUT, env=env)
            try:
                while True:
                    try:
                        rpc_result(rpc_http(port, request("eth_blockNumber"))[1])
                        break
                    except (OSError, http.client.HTTPException):
                        if process.poll() is not None or time.perf_counter() - start > 30:
                            raise RuntimeError(f"{lane} startup failed, see {work / 'node.log'}")
                        time.sleep(0.01)
                yield port, engine_port, process.pid, (time.perf_counter() - start) * 1000
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def rpc_result(response):
    assert response["jsonrpc"] == "2.0" and "result" in response, response
    return response["result"]


def rpc_code(response):
    return response.get("error", {}).get("code")


def by_id(answers):
    assert isinstance(answers, list), answers
    indexed = {item["id"]: item for item in answers}
    assert len(indexed) == len(answers), answers
    return indexed


def pilot(selector="latest", address=ADDRESS, slot=KNOWN_SLOT):
    return [
        request("eth_getBalance", [address, selector], "balance"),
        request("eth_getStorageAt", [address, slot, selector], "storage"),
        request("eth_getBlockByNumber", [selector, False], "block"),
    ]


def matrix(port, engine_port, lane, candidate):
    output = {"preserved": {}, "parameterCases": {}}

    def check_id(query, answer):
        assert answer["id"] == query["id"], (query, answer)
        assert answer["jsonrpc"] == "2.0", answer

    def execute(query):
        status, answer = rpc_http(port, query)
        assert status == 200, status
        check_id(query, answer)
        return answer

    def healthy():
        assert isinstance(rpc_result(execute(request("eth_blockNumber"))), str)

    valid = pilot()
    positive = [execute(query) for query in valid]
    for answer in positive:
        rpc_result(answer)
    hash_value = positive[2]["result"]["hash"]
    for selector in ["latest", "pending", "earliest", "safe", "finalized", "0x0",
                     hash_value, "0x00", "0x1", "0x7fffffffffffffff", UNKNOWN_HASH]:
        for query in pilot(selector):
            label = f"{query['id']}:{selector}"
            output["preserved"][label] = execute(query)
    assert output["preserved"]["block:0x1"]["result"] is None
    assert output["preserved"]["block:" + UNKNOWN_HASH]["result"] is None

    for query in [
        request("eth_getBalance", ["0X" + ADDRESS[2:], "latest"], "uppercase-address-prefix"),
        request("eth_getBalance", [ADDRESS[2:], "latest"], "unprefixed-address"),
        request("eth_getStorageAt", [ADDRESS, "0X000A", "latest"], "uppercase-slot-prefix"),
        request("eth_getStorageAt", [ADDRESS, "A", "latest"], "unprefixed-slot"),
        request("eth_getStorageAt", [ADDRESS, "0x" + "0" * 63 + "a", "latest"], "padded-slot"),
        request("eth_getStorageAt", [ADDRESS, "0xa", "latest"], "odd-length-slot"),
        request("eth_getBlockByNumber", ["0x0", True], "full-transactions"),
        request("eth_getBalance", [ADDRESS], "balance-default-selector"),
        request("eth_getStorageAt", [ADDRESS, KNOWN_SLOT], "storage-default-selector"),
        request("eth_getStorageAt", [ADDRESS, "", "latest"], "storage-empty-slot"),
        request("eth_getStorageAt", [ADDRESS, "0x", "latest"], "storage-empty-prefixed-slot"),
    ]:
        response = execute(query)
        rpc_result(response)
        output["preserved"][query["id"]] = response

    invalid = {
        "balance-absent-params": ["eth_getBalance", None],
        "balance-null-address": ["eth_getBalance", [None, "latest"]],
        "balance-null-array-item": ["eth_getBalance", [None]],
        "balance-null-selector": ["eth_getBalance", [ADDRESS, None]],
        "balance-extra-param": ["eth_getBalance", [ADDRESS, "latest", False]],
        "balance-empty-address": ["eth_getBalance", ["", "latest"]],
        "balance-short-address": ["eth_getBalance", ["0x1", "latest"]],
        "balance-short-invalid-address": ["eth_getBalance", ["0xzz", "latest"]],
        "balance-invalid-tag": ["eth_getBalance", [ADDRESS, "tip"]],
        "balance-eip1898-number": ["eth_getBalance", [ADDRESS, {"blockNumber": "0x0"}]],
        "balance-eip1898-hash": ["eth_getBalance", [ADDRESS, {"blockHash": hash_value,
                                                             "requireCanonical": True}]],
        "storage-invalid-address-type": ["eth_getStorageAt", [False, KNOWN_SLOT, "latest"]],
        "storage-null-slot": ["eth_getStorageAt", [ADDRESS, None, "latest"]],
        "storage-long-slot": ["eth_getStorageAt", [ADDRESS, "0x" + "a" * 65, "latest"]],
        "storage-invalid-slot": ["eth_getStorageAt", [ADDRESS, "0xzz", "latest"]],
        "block-missing-bool": ["eth_getBlockByNumber", ["0x0"]],
        "block-numeric-bool": ["eth_getBlockByNumber", ["0x0", 1]],
        "block-null-selector": ["eth_getBlockByNumber", [None, False]],
        "block-numeric-selector": ["eth_getBlockByNumber", [0, False]],
        "block-empty-selector": ["eth_getBlockByNumber", ["", False]],
        "block-invalid-selector": ["eth_getBlockByNumber", ["0xg", False]],
        "block-overflow-selector": ["eth_getBlockByNumber", ["0x10000000000000000", False]],
    }
    malformed = []
    for label, (method, params) in invalid.items():
        query = request(method, params, label)
        if params is None:
            del query["params"]
        outcome = execute(query)
        if candidate:
            assert rpc_code(outcome) == -32602, (label, outcome)
            assert "params" in outcome["error"]["message"] and len(outcome["error"]["message"]) < 256, outcome
        output["parameterCases"][label] = outcome
        malformed.append((query, outcome))
        healthy()

    for query in valid:
        null_params = dict(query, params=None)
        response = execute(null_params)
        assert rpc_code(response) == -32600, response
        output["preserved"][query["id"] + ":null-params-envelope"] = response
        healthy()

    invalid_query, invalid_answer = next(item for item in malformed if item[0]["id"] == "balance-extra-param")
    # Compare actual WebSocket wire results with HTTP, including single-element batch shape.
    with WebSocket(port) as ws:
        rpc_result(ws.exchange(request("eth_blockNumber")))
        for q, expected in zip(valid, positive, strict=True):
            assert ws.exchange(q) == expected, q
            assert ws.exchange([q]) == [expected]
            assert rpc_http(port, [q]) == (200, [expected])
        for q, expected in malformed:
            seen = ws.exchange(q)
            # A diagnostic may include transport-local backtraces in the old implementation.
            assert rpc_code(seen) == rpc_code(expected)
            assert seen.get("result") == expected.get("result")
            check_id(q, seen)
        mixed = [valid[0], invalid_query, valid[1], valid[2]]
        mixed_expected = [positive[0], invalid_answer, positive[1], positive[2]]
        for answers in [rpc_http(port, mixed)[1], ws.exchange(mixed)]:
            assert by_id(answers) == by_id(mixed_expected)
        healthy()

    for method in ("eth_x7TestUnknown", "debug_dbGet"):
        message = execute(request(method, [None]))
        assert rpc_code(message) == -32601, message
        message = execute(request(method, {}))
        assert rpc_code(message) == -32600, message
        healthy()

    bad_percentiles = ["0x1", "latest", "invalid"]
    for name, params in [("valid", ["0x1", "latest", []]), ("invalid", bad_percentiles)]:
        answer = execute(request("eth_feeHistory", params, "feeHistory-" + name))
        if lane == "fisco":
            assert rpc_code(answer) == -32601, answer
        elif name == "invalid":
            assert rpc_code(answer) == -32602, answer
        else:
            rpc_result(answer)
        output["preserved"][answer["id"]] = answer
        healthy()
    answer = execute(request("txpool_status", [None]))
    if lane != "op":
        assert rpc_code(answer) == -32601, answer
    else:
        rpc_result(answer)
    output["preserved"]["txpool_status"] = answer
    healthy()

    if lane in {"eth", "op"}:
        good = request("engine_exchangeCapabilities", [[]])
        status, answer = rpc_http(engine_port, good, jwt())
        assert status == 200 and isinstance(rpc_result(answer), list), (status, answer)
        denied = [rpc_http(engine_port, req, "invalid-jwt") for req in (good, invalid_query)]
        assert denied[0] == denied[1] and denied[0][0] in {401, 403}, denied
        assert "JWT authentication failed" in denied[0][1]["error"]["message"], denied
        healthy()
        assert rpc_result(rpc_http(engine_port, good, jwt())[1])
        output["jwtStatus"] = denied[0][0]

    at_limit = [request("eth_blockNumber", request_id=i) for i in range(MAX_BATCH)]
    above_limit = at_limit + [request("eth_blockNumber", request_id=MAX_BATCH)]
    with WebSocket(port) as ws:
        for send in (lambda obj: rpc_http(port, obj)[1], ws.exchange):
            assert len(send(at_limit)) == MAX_BATCH
            limited = send(above_limit)
            assert rpc_code(limited) == -32600 and "limit" in limited["error"]["message"], limited
            healthy()
    below_limit = wire(request("eth_blockNumber"))
    below_limit += b" " * (LIMIT - len(below_limit))
    rpc_result(rpc_http(port, below_limit)[1])
    try:
        result = rpc_http(port, below_limit + b" ")
    except (ConnectionResetError, http.client.RemoteDisconnected, BrokenPipeError):
        pass
    else:
        raise AssertionError(("HTTP body limit did not close the request", result))
    healthy()
    with WebSocket(port) as ws:
        rpc_result(ws.exchange(below_limit))
        try:
            ws.exchange(below_limit + b" ")
        except ConnectionAbortedError as exc:
            assert exc.args == (1009,), exc
        else:
            raise AssertionError("WebSocket message limit did not close the request")
    healthy()
    # A bounded nested object tests the existing parser/renderer and schema boundary together.
    # It fits the HTTP cap and is rejected as the typed argument, without excessive load.
    nested = "leaf"
    for _ in range(25):
        nested = {"field": nested}
    deep = execute(request("eth_getBalance", [nested, "latest"]))
    if candidate:
        assert rpc_code(deep) == -32602, deep
    healthy()
    output["resourceLimits"] = {"httpBody": LIMIT, "batch": MAX_BATCH, "nestedField": rpc_code(deep)}
    return output


def rss_bytes(pid):
    return int(local_command("ps", "-o", "rss=", "-p", str(pid)).strip()) * 1024


def workload(port, duration, batch, expected):
    # Keep the genesis profile and exact anticipated JSON results pinned throughout the run.
    queries = pilot()[:2] + [pilot("0x0")[2]]
    if batch:
        queries = [[dict(query, id=i) for i, query in enumerate(queries * 2)]]
        expected = [by_id([dict(answer, id=i) for i, answer in enumerate(expected * 2)])]
    iterations = [(wire(q), a) for q, a in zip(queries, expected, strict=True)]
    deadline = time.perf_counter() + duration

    def client_loop():
        latencies = []
        with closing(connection(port)) as client:
            while time.perf_counter() < deadline:
                payload, answer = iterations[len(latencies) % len(iterations)]
                start = time.perf_counter()
                status, response = rpc_http(port, payload, client=client)
                latencies.append((time.perf_counter() - start) * 1000)
                comparable = by_id(response) if batch else response
                assert status == 200 and comparable == answer, (payload, status, response, answer)
        return latencies

    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=4) as pool:
        latencies = sorted(x for lats in pool.map(lambda _: client_loop(), range(4)) for x in lats)
    elapsed = time.perf_counter() - start
    requests = len(latencies) * (6 if batch else 1)
    return {"elapsedSec": elapsed, "rpcCount": requests, "rpcsPerSec": requests / elapsed,
            "p50Ms": statistics.median(latencies), "p99Ms": latencies[int(0.99 * len(latencies))]}


def describe(binary):
    with binary.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": str(binary), "sha256": digest, "version": local_command(binary, "-v").decode()}


def performance(fixture, out, binaries):
    expected = None
    results = {label: [] for label in binaries}
    for index in range(4):
        order = list(binaries)
        if index % 2:
            order.reverse()
        for label in order:
            work = out / f"perf-{index}-{label}"
            with fixture.start(work, binaries[label], "eth") as (port, _, pid, startup):
                actual = [rpc_http(port, q)[1] for q in pilot()[:2] + [pilot("0x0")[2]]]
                for answer in actual:
                    rpc_result(answer)
                if expected is None:
                    expected = actual
                assert actual == expected, (actual, expected)
                measured = {"startupMs": startup}
                for name, batch in [("single", False), ("batch", True)]:
                    workload(port, 1, batch, expected)
                    measured[name] = workload(port, 3, batch, expected)
                measured["rssBytes"] = rss_bytes(pid)
                results[label].append(measured)
                print(json.dumps({"round": index, "binary": label, **measured}), flush=True)
    return results


def allocations(fixture, out, binaries, counter):
    results = {}
    queries = pilot()[:2] + [pilot("0x0")[2]]
    launch_env = dict(os.environ, DYLD_INSERT_LIBRARIES=str(counter))
    for label, binary in binaries.items():
        runtime = out / f"alloc-{label}"
        counter_file = out / f"alloc-{label}.counter"
        counter_file.write_bytes(bytes(8))
        env = dict(launch_env, X7_MALLOC_COUNT_FILE=str(counter_file))
        with fixture.start(runtime, binary, "eth", env=env) as (port, _, _, _):
            def count():
                return struct.unpack("=Q", counter_file.read_bytes())[0]

            results[label] = {}
            for name, batch in [("single", False), ("batch", True)]:
                payloads = queries
                if batch:
                    payloads = [[dict(q, id=i) for i, q in enumerate(queries * 2)]]
                with closing(connection(port)) as client:
                    def send():
                        for i in range(120):
                            status, response = rpc_http(port, payloads[i % len(payloads)], client=client)
                            assert status == 200
                            for item in (response if batch else [response]):
                                rpc_result(item)

                    send()
                    start = count()
                    begin = time.perf_counter()
                    send()
                    elapsed = time.perf_counter() - begin
                    end = count()
                    time.sleep(elapsed)
                    idle = count() - end
                    assert end > start, "allocation counter produced no events"
                    items = 120 * (6 if batch else 1)
                    results[label][name] = {
                        "events": end - start,
                        "comparableIdleEvents": idle,
                        "rpcCount": items,
                        "eventsPerRpc": (end - start) / items,
                    }
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path)
    parser.add_argument("--genesis-tool", type=Path, required=True)
    parser.add_argument("--malloc-counter", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--lanes", choices=("eth", "op", "fisco"), nargs="+", default=["eth", "op", "fisco"])
    parser.add_argument("--mode", choices=("functional", "performance", "both"), default="both")
    args = parser.parse_args()
    if not __debug__:
        parser.error("run without Python optimization so test assertions are active")
    if args.mode != "functional" and not args.new:
        parser.error("paired performance requires --new")
    out = args.out.resolve()
    out.mkdir(mode=0o700)
    fixture = Fixture(out / "fixture", args.genesis_tool.resolve())
    binaries = {"old": args.old.resolve()}
    if args.new:
        binaries["new"] = args.new.resolve()
    report = {"binaries": {name: describe(path) for name, path in binaries.items()},
              "os": local_command("uname", "-msr").decode().strip(),
              "compiler": local_command("c++", "--version").decode(),
              "python": local_command("python3", "--version").decode().strip(),
              "hardware": local_command("sysctl", "-n", "machdep.cpu.brand_string",
                                        "hw.ncpu", "hw.memsize").decode().splitlines()}
    try:
        if args.mode in {"functional", "both"}:
            report["functional"] = {}
            for label, binary in binaries.items():
                report["functional"][label] = {}
                for lane in args.lanes:
                    with fixture.start(out / f"matrix-{label}-{lane}", binary, lane, limits=True) as (
                        port, engine_port, _, startup
                    ):
                        report["functional"][label][lane] = matrix(port, engine_port, lane, label == "new")
                    print(f"Finished {label}/{lane}", flush=True)
            if "new" in binaries:
                for lane in args.lanes:
                    before, after = (report["functional"][label][lane] for label in ("old", "new"))
                    assert before["preserved"] == after["preserved"], lane
                    assert before.get("jwtStatus") == after.get("jwtStatus"), lane
        if args.mode in {"performance", "both"}:
            report["performance"] = performance(fixture, out, binaries)
            if args.malloc_counter:
                report["allocations"] = allocations(fixture, out, binaries, args.malloc_counter.resolve())
    finally:
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
