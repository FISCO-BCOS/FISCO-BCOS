#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Exercise RocksDB engine metadata across SIGKILL using a local production node."""

import argparse
import json
from pathlib import Path

from l1_call_tracer_parity import (
    FIXTURES, Processes, build_payload, choice, node_config, ready, require_valid,
)


def forkchoice(engine, head, safe, finalized):
    state = dict(choice(head), safeBlockHash=safe, finalizedBlockHash=finalized)
    return engine.raw("engine_forkchoiceUpdatedV3", [state, None])


def run(args, processes):
    web, engine = node_config(args.tool, args.binary, args.out, processes, None, window=1)
    genesis = web.result("eth_getBlockByNumber", ["0x0", False])
    timestamp = int(genesis["timestamp"], 16)

    def accept(payload):
        require_valid(engine.result("engine_newPayloadV4", payload))
        return payload[0]["blockHash"]

    sibling = build_payload(engine, genesis["hash"], timestamp + 1)
    finalized = accept(build_payload(engine, genesis["hash"], timestamp + 2))
    safe = accept(build_payload(engine, finalized, timestamp + 3))
    require_valid(forkchoice(engine, safe, safe, finalized)["result"])

    tx = json.loads((FIXTURES / "transactions.json").read_text())["transactions"][0]
    block = build_payload(engine, safe, timestamp + 4, [tx["raw"]])
    tip = accept(block)
    methods = ("eth_getTransactionByHash", "eth_getTransactionReceipt")
    for method in methods:
        assert web.result(method, [tx["hash"]])["blockHash"] == tip

    rejected = engine.result("engine_newPayloadV4", sibling)
    assert rejected["status"] == "INVALID" and "finalized" in rejected["validationError"], rejected

    child, _ = processes.children[-1]
    child.kill()
    assert child.wait(timeout=15) == -9
    restarted = processes.start([args.binary, "-c", "config.ini", "-g", "config.genesis"],
                                args.out / "fisco", args.out / "fisco-restarted.log")
    ready(restarted, web)
    for tag, expected in [("safe", safe), ("finalized", finalized), ("latest", tip)]:
        assert web.result("eth_getBlockByNumber", [tag, False])["hash"] == expected, tag

    error = forkchoice(engine, genesis["hash"], genesis["hash"], genesis["hash"])["error"]
    assert error["code"] == -38002, error
    rejected = engine.result("engine_newPayloadV4", sibling)
    assert rejected["status"] == "INVALID" and "finalized" in rejected["validationError"], rejected

    require_valid(forkchoice(engine, safe, safe, finalized)["result"])
    assert web.result("eth_getBlockByNumber", ["latest", False])["hash"] == safe
    for method in methods:
        assert web.result(method, [tx["hash"]]) is None, method

    assert accept(block) == tip
    for method in methods:
        assert web.result(method, [tx["hash"]])["blockHash"] == tip
    print("PASS SIGKILL restart, finalized boundary, and transaction rollback")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "tool", "out"):
        parser.add_argument(f"--{name}", type=lambda value: Path(value).resolve(), required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    processes = Processes()
    try:
        run(args, processes)
    finally:
        processes.close()


if __name__ == "__main__":
    main()
