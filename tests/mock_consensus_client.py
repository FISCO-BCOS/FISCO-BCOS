#!/usr/bin/env python3
"""
mock_consensus_client.py — FISCO-BCOS Engine API Smoke Test (L1 Engine API)

模拟一个共识客户端对 executor_version=2（Eth 车道，L1 Engine API，Cancun 链）节点的
Engine API 调用流程:

  1. engine_exchangeCapabilities   — 节点实现的全部方法(含 V1-V3 旧版本)
  2. engine_forkchoiceUpdatedV3    — 无 payloadAttributes(纯状态更新)
  3. engine_forkchoiceUpdatedV3    — 带 payloadAttributes(不带 OP 属性、不注入交易)
  4. engine_getPayloadV3           — 校验 V3 响应形状
  5. engine_newPayloadV3           — [payload, [], beaconRoot] 提交,等区块成为 eth_* 头
  6. 0x7e deposit 负测             — deposit 是 OP 车道扩展,Eth 车道 FCU 终态 INVALID
  7. V2 FCU 负测                   — Cancun 链上 V2 FCU 回 -38003
  8. 负测                          — 未实现版本 -38005、未知 payloadId -38001、缺参 -32602

用法:
    pip install requests
    python3 mock_consensus_client.py [RPC_URL] [JWT_SECRET_FILE]

退出码: 0 = 全部通过, 1 = 有测试失败
"""

import json
import sys
import time
from typing import Any, Dict, List, Optional

try:
    import requests
except ImportError:
    print("ERROR: 'requests' module is required. Install with:", file=sys.stderr)
    print("  pip3 install requests", file=sys.stderr)
    print("  pip3 install --break-system-packages requests  # macOS", file=sys.stderr)
    sys.exit(1)

# ---- Configuration ----

RPC_URL = "http://127.0.0.1:8545"
HEADERS = {"Content-Type": "application/json"}
TIMEOUT = 30

ZERO_HASH = "0x" + "00" * 32
FEE_RECIPIENT = "0x0000000000000000000000000000000000000001"
PREV_RANDAO = "0x" + "00" * 31 + "01"

# Everything the node implements (must match detail::supportedCapabilities()). The list is
# NOT narrowed to the active fork: geth advertises its whole method set and lets the CL
# pick, and the older versions stay served.
EXPECTED_CAPABILITIES = {
    "engine_exchangeCapabilities",
    "engine_forkchoiceUpdatedV1",
    "engine_forkchoiceUpdatedV2",
    "engine_forkchoiceUpdatedV3",
    "engine_getPayloadV1",
    "engine_getPayloadV2",
    "engine_getPayloadV3",
    "engine_getPayloadV4",
    "engine_getPayloadV5",
    "engine_newPayloadV1",
    "engine_newPayloadV2",
    "engine_newPayloadV3",
    "engine_newPayloadV4",
    "engine_getPayloadBodiesByHashV1",
    "engine_getPayloadBodiesByRangeV1",
    "engine_getBlobsV1",
    "engine_getClientVersionV1",
    "engine_exchangeClientVersionV1",
}

# The one genuinely unimplemented version: routable, answers -38005, never advertised.
UNIMPLEMENTED_METHODS = ("engine_forkchoiceUpdatedV4",)

# ---- Minimal RLP encoder (enough for the deposit fixture) ----


def _rlp_bytes(payload: bytes) -> bytes:
    if len(payload) == 1 and payload[0] < 0x80:
        return payload
    if len(payload) > 55:
        raise ValueError(f"RLP payload too long: {len(payload)} > 55")
    return bytes([0x80 + len(payload)]) + payload


def _rlp_int(value: int) -> bytes:
    if value == 0:
        return b"\x80"
    return _rlp_bytes(value.to_bytes((value.bit_length() + 7) // 8, "big"))


def _rlp_list(items: List[bytes]) -> bytes:
    body = b"".join(items)
    if len(body) <= 55:
        return bytes([0xC0 + len(body)]) + body
    length = len(body).to_bytes((len(body).bit_length() + 7) // 8, "big")
    return bytes([0xF7 + len(length)]) + length + body


def build_deposit_raw() -> str:
    """dep-1: 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data]).

    Fixed L1 info; same field shapes as the EngineRawTxB2B3Test C++ fixture.
    """
    body = _rlp_list(
        [
            _rlp_bytes(bytes.fromhex("11" * 32)),  # sourceHash
            _rlp_bytes(bytes.fromhex("22" * 20)),  # from
            _rlp_bytes(bytes.fromhex("33" * 20)),  # to
            _rlp_int(1000),  # mint
            _rlp_int(7),  # value
            _rlp_int(21000),  # gas
            _rlp_int(0),  # isSystemTx = false
            _rlp_bytes(bytes.fromhex("deadbeef")),  # data
        ]
    )
    return "0x7e" + body.hex()


DEPOSIT_RAW = build_deposit_raw()

# ---- Helpers ----

PASS = 0
FAIL = 0

GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[1;33m"
NC = "\033[0m"


def _log_test(name: str) -> None:
    print(f"{GREEN}[TEST]{NC} {name}")


def _log_pass() -> None:
    global PASS
    PASS += 1
    print(f"  {GREEN}✅ PASSED{NC}")


def _log_fail(msg: str) -> None:
    global FAIL
    FAIL += 1
    print(f"  {RED}❌ FAILED: {msg}{NC}")


def _log_info(msg: str) -> None:
    print(f"  {YELLOW}ℹ️  {msg}{NC}")


def rpc_raw(method: str, params: List[Any], req_id: int = 1) -> Dict[str, Any]:
    """Send a JSON-RPC request, return the full response dict (error included)."""
    payload: Dict[str, Any] = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params,
    }
    resp = requests.post(RPC_URL, json=payload, headers=HEADERS, timeout=TIMEOUT)
    resp.raise_for_status()
    return resp.json()


def rpc_result(method: str, params: List[Any], req_id: int = 1) -> Any:
    data = rpc_raw(method, params, req_id)
    if "error" in data:
        raise RuntimeError(f"RPC error [{method}]: {data['error']}")
    return data["result"]


def expect_error_code(method: str, params: List[Any], expected_code: int) -> None:
    """Assert a call fails with exactly the given JSON-RPC error code, id echoed."""
    _log_test(f"{method} -> error {expected_code}")
    req_id = 99
    data = rpc_raw(method, params, req_id=req_id)
    if "error" not in data:
        _log_fail(f"expected error {expected_code}, got success: {data.get('result')}")
        return
    code = data["error"]["code"]
    if code != expected_code:
        _log_fail(f"expected {expected_code}, got {code}: {data['error']}")
        return
    # JSON-RPC 2.0 requires the id to be echoed on error responses too; clients that
    # correlate concurrent/batched calls by id depend on it.
    if data.get("id") != req_id:
        _log_fail(f"error response did not echo the request id: got {data.get('id')!r}")
        return
    _log_info(f"message = {data['error'].get('message', '')[:80]}")
    _log_pass()


# ---- Test Cases ----


def test_exchange_capabilities() -> None:
    """The node advertises everything it implements, older versions included."""
    _log_test("engine_exchangeCapabilities (implemented surface)")

    result = rpc_result("engine_exchangeCapabilities", [sorted(EXPECTED_CAPABILITIES)])
    returned = set(result)

    if returned != EXPECTED_CAPABILITIES:
        _log_fail(
            f"capability mismatch: missing={EXPECTED_CAPABILITIES - returned} "
            f"extra={returned - EXPECTED_CAPABILITIES}"
        )
        return

    _log_info(f"{len(returned)} capabilities advertised, V1-V3 included")
    _log_pass()


def get_head_hash() -> str:
    block = rpc_result("eth_getBlockByNumber", ["latest", False])
    return block["hash"]


def test_forkchoice_v3_without_payload() -> None:
    _log_test("engine_forkchoiceUpdatedV3 (without payloadAttributes)")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }
    result = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, None])
    status = result["payloadStatus"]["status"]
    _log_info(f"payloadStatus.status = {status}")
    if status in ("VALID", "SYNCING"):
        _log_pass()
    else:
        _log_fail(f"Unexpected status: {status}")


_timestamp_bump = 0


def next_timestamp() -> str:
    """Engine wire timestamps are Unix SECONDS (execution-apis)."""
    global _timestamp_bump
    _timestamp_bump += 1
    return hex(int(time.time()) + _timestamp_bump)


def run_block_flow() -> bool:
    """One L1 Cancun block: FCU V3 (no OP attributes, no transactions) -> getPayloadV3 ->
    newPayloadV3. The payload is built from the txpool; the injected-deposit rejection is
    pinned separately by test_deposit_envelope_is_rejected_on_eth_lane.
    """
    get_method = "engine_getPayloadV3"
    new_method = "engine_newPayloadV3"
    _log_test(f"FCU V3 + {get_method} + {new_method}")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }
    timestamp_hex = next_timestamp()
    sent_seconds = int(timestamp_hex, 16)
    payload_attrs = {
        "timestamp": timestamp_hex,
        "prevRandao": PREV_RANDAO,
        "suggestedFeeRecipient": FEE_RECIPIENT,
        "withdrawals": [],
        "parentBeaconBlockRoot": ZERO_HASH,
    }

    fcu = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, payload_attrs])
    status = fcu["payloadStatus"]["status"]
    payload_id = fcu.get("payloadId")
    _log_info(f"FCU status={status}, payloadId={payload_id}")
    if status != "VALID" or not payload_id:
        _log_fail(f"FCU did not build a payload: status={status}, payloadId={payload_id}")
        return False

    result = rpc_result(get_method, [payload_id])

    # Response shape: V3 (Cancun).
    fields = ["executionPayload", "blockValue", "blobsBundle", "shouldOverrideBuilder"]
    for field in fields:
        if field not in result:
            _log_fail(f"{get_method} response missing '{field}': {sorted(result.keys())}")
            return False
    bundle = result["blobsBundle"]
    if any(bundle.get(k) != [] for k in ("commitments", "proofs", "blobs")):
        _log_fail(f"blobsBundle must be three empty arrays, got {bundle}")
        return False
    if result["shouldOverrideBuilder"] is not False:
        _log_fail("shouldOverrideBuilder must be false")
        return False

    payload = result["executionPayload"]
    if payload.get("withdrawals") != []:
        _log_fail(f"executionPayload withdrawals must be [] (Cancun V3 shape): {payload}")
        return False

    # Unit check, wire side: the Engine boundary speaks Unix SECONDS in both directions.
    # This catches a ONE-SIDED break only (parse or serialize, not both): removing the
    # conversion from both cancels out on the wire and leaves this equality holding. The
    # symmetric case is caught by the committed-block check further down, which reads the
    # header through eth_getBlockByNumber.
    got_seconds = int(payload["timestamp"], 16)
    if abs(got_seconds - sent_seconds) > 1:
        _log_fail(
            f"{get_method} timestamp is not the Unix seconds we sent: "
            f"sent {sent_seconds}, got {got_seconds}"
        )
        return False

    # The txpool is the only tx source on this harness, so a 0x7e envelope in the payload
    # would mean a deposit was executed after all.
    txs = payload["transactions"]
    if any(tx.lower().startswith("0x7e") for tx in txs):
        _log_fail(f"Eth lane built a deposit envelope into the payload: {txs[:2]}")
        return False
    _log_info(f"payload carries {len(txs)} txpool tx(s)")

    beacon_root = result.get("parentBeaconBlockRoot", ZERO_HASH)
    new_params = [payload, [], beacon_root]
    new_status = rpc_result(new_method, new_params)
    # VALID only. ACCEPTED would mean the node acknowledged the block without validating
    # or storing it, which is exactly the escape M9 removed — accepting it here would let
    # that regression back in silently.
    _log_info(f"{new_method} status = {new_status['status']}")
    if new_status["status"] != "VALID":
        _log_fail(f"{new_method} did not answer VALID for the built payload: {new_status}")
        return False

    # Wait for the committed block to become the eth_* head (ledger rows are written
    # inside newPayload, so this is normally immediate).
    committed_hash = payload["blockHash"]
    for _ in range(20):
        if get_head_hash() == committed_hash:
            break
        time.sleep(0.5)
    else:
        _log_fail(f"committed block {committed_hash} never became the eth_* head")
        return False

    # Unit check, storage side: eth_getBlockByNumber reports seconds too
    # (BlockResponse.cpp divides the millisecond header timestamp by 1000), so the
    # committed block header must carry the seconds the CL asked for.
    committed_block = rpc_result("eth_getBlockByNumber", [payload["blockNumber"], False])
    block_seconds = int(committed_block["timestamp"], 16)
    if abs(block_seconds - sent_seconds) > 1:
        _log_fail(
            f"committed block timestamp is not the Unix seconds we sent: "
            f"sent {sent_seconds}, block reports {block_seconds}"
        )
        return False
    _log_info(f"block timestamp = {block_seconds} (sent {sent_seconds})")

    _log_pass()
    return True


def test_deposit_envelope_is_rejected_on_eth_lane() -> None:
    """Eth lane: a 0x7e deposit in payloadAttributes.transactions must be terminal INVALID.

    Deposits are an OP-Stack payloadAttributes extension; the Eth build path decodes the
    envelope with allowDeposit=false and answers it as undecodable, which
    forkchoiceUpdated maps to a terminal INVALID (fcInvalidIfUndecodable). A VALID here
    would mean the Eth lane executes deposits again — the exact regression 071c45fec
    closed.
    """
    _log_test("FCU V3 with a deposit envelope -> INVALID on the Eth lane")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }
    payload_attrs = {
        "timestamp": next_timestamp(),
        "prevRandao": PREV_RANDAO,
        "suggestedFeeRecipient": FEE_RECIPIENT,
        "withdrawals": [],
        "parentBeaconBlockRoot": ZERO_HASH,
        "transactions": [DEPOSIT_RAW],
        "noTxPool": True,
    }

    fcu = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, payload_attrs])
    status = fcu["payloadStatus"]["status"]
    if status != "INVALID" or fcu.get("payloadId"):
        _log_fail(f"Eth lane accepted a deposit envelope: status={status}, payloadId={fcu.get('payloadId')}")
        return
    _log_info(f"status=INVALID ({fcu['payloadStatus'].get('validationError')})")
    _log_pass()


def run_v2_block_flow() -> None:
    """V2 FCU on a CANCUN harness chain is refused.

    A V2 forkchoiceUpdated cannot express the CANCUN attribute shape
    (parentBeaconBlockRoot). The L1 Engine API refuses it with -38003 Invalid payload
    attributes (the node's current answer; geth answers -38005 Unsupported fork). The V2
    build loop itself is covered by the engine unit tests on a SHANGHAI fixture.
    """
    _log_test("FCU V2 on a Cancun chain -> -38003")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }
    payload_attrs = {
        "timestamp": next_timestamp(),
        "prevRandao": PREV_RANDAO,
        "suggestedFeeRecipient": FEE_RECIPIENT,
        "withdrawals": [],
    }

    expect_error_code(
        "engine_forkchoiceUpdatedV2", [fc_state, payload_attrs], -38003
    )
    _log_pass()


def test_negative_cases() -> None:
    """Error semantics: -38005 for unimplemented versions, -38001 unknown payload, -32602 shape."""
    fc_state_head = get_head_hash()
    fc_state = {
        "headBlockHash": fc_state_head,
        "safeBlockHash": fc_state_head,
        "finalizedBlockHash": fc_state_head,
    }
    # Method versions this node does not implement answer -38005 Unsupported fork rather
    # than -32601: they are routed, just not built. Every OTHER version is served — the
    # V1-V3 flows above are the positive side of that.
    expect_error_code("engine_forkchoiceUpdatedV4", [fc_state, None], -38005)

    # getPayloadV4 IS served (Isthmus): an unknown id is -38001, not -38005.
    expect_error_code("engine_getPayloadV4", ["0x00000000deadbeef"], -38001)

    # Unknown payloadId -> -38001 Unknown payload.
    expect_error_code("engine_getPayloadV5", ["0x00000000deadbeef"], -38001)

    # Missing/malformed params -> -32602 InvalidParams.
    expect_error_code("engine_getPayloadV5", [], -32602)
    expect_error_code("engine_newPayloadV4", [], -32602)

    # forkchoiceUpdatedV3 payloadAttributes are parsed strictly: a JSON-number timestamp
    # used to be stringified and then read as HEX (123 -> 0x123), and malformed hex
    # escaped as -32603 InternalError.
    def attrs_with(**overrides: Any) -> Dict[str, Any]:
        attrs = {
            "timestamp": next_timestamp(),
            "prevRandao": PREV_RANDAO,
            "suggestedFeeRecipient": FEE_RECIPIENT,
            "withdrawals": [],
            "parentBeaconBlockRoot": ZERO_HASH,
        }
        attrs.update(overrides)
        return attrs

    expect_error_code("engine_forkchoiceUpdatedV3", [fc_state, attrs_with(timestamp=123)], -32602)
    expect_error_code(
        "engine_forkchoiceUpdatedV3", [fc_state, attrs_with(timestamp="0xnothex")], -32602
    )
    expect_error_code("engine_forkchoiceUpdatedV3", [fc_state, attrs_with(gasLimit=123)], -32602)
    expect_error_code("engine_forkchoiceUpdatedV3", [fc_state, attrs_with(prevRandao=[])], -32602)
    expect_error_code("engine_forkchoiceUpdatedV3", [fc_state, "not-an-object"], -32602)

    # newPayloadV4 blob-hash elements are type-checked, not just the array itself.
    expect_error_code("engine_newPayloadV4", [{}, [123], ZERO_HASH, []], -32602)
    expect_error_code("engine_newPayloadV4", [{}, [[]], ZERO_HASH, []], -32602)
    # V1-V3 had no params[1] shape gate at all, so this used to be accepted as VALID.
    expect_error_code("engine_newPayloadV3", [{}, "notarray", ZERO_HASH], -32602)

    # exchangeCapabilities is the first method a CL calls; a non-string entry used to
    # escape as -32603 with boost's diagnostic string attached.
    expect_error_code("engine_exchangeCapabilities", [[[]]], -32602)

    # Unknown method -> -32601 MethodNotFound.
    expect_error_code("engine_unknownMethod", [], -32601)


# ---- Main ----


def generate_jwt(secret_file: str) -> str:
    import base64
    import hashlib
    import hmac

    with open(secret_file, "r") as f:
        secret = bytes.fromhex(f.read().strip())
    h = base64.urlsafe_b64encode(json.dumps({"alg": "HS256", "typ": "JWT"}).encode()).rstrip(b"=").decode()
    p = base64.urlsafe_b64encode(
        json.dumps({"iat": int(time.time()), "id": "12345678", "clv": "FISCO-BCOS"}).encode()
    ).rstrip(b"=").decode()
    sig = base64.urlsafe_b64encode(
        hmac.new(secret, f"{h}.{p}".encode(), hashlib.sha256).digest()
    ).rstrip(b"=").decode()
    return f"{h}.{p}.{sig}"


def main() -> int:
    global RPC_URL

    positional = [a for a in sys.argv[1:] if not a.startswith("--")]
    if positional:
        RPC_URL = positional[0]
    if len(positional) > 1:
        jwt_token = generate_jwt(positional[1])
        HEADERS["Authorization"] = f"Bearer {jwt_token}"
        _log_info("JWT token generated")

    print("=" * 50)
    print("  FISCO-BCOS Engine API Smoke Test (L1 Engine API)")
    print(f"  Target: {RPC_URL}")
    print("=" * 50)
    print()

    try:
        # 1. Capability negotiation.
        test_exchange_capabilities()

        # 2. Pure forkchoice update (no payload build).
        test_forkchoice_v3_without_payload()

        # 3.-6. One deposit-free Cancun block flow; deposits are refused on this lane.
        if run_block_flow():
            test_deposit_envelope_is_rejected_on_eth_lane()

        # 7. V2 FCU on the Cancun chain.
        run_v2_block_flow()

        # 8. Error semantics.
        test_negative_cases()

    except requests.ConnectionError:
        print(f"\n{RED}❌ Cannot connect to {RPC_URL}{NC}")
        print("   Make sure the node is running and the port is reachable.")
        return 1
    except Exception as e:
        print(f"\n{RED}❌ TEST ERROR: {e}{NC}")
        import traceback

        traceback.print_exc()
        return 1

    # ---- Summary ----
    print()
    print("=" * 50)
    print(f"  Results: {GREEN}{PASS} passed{NC}, {RED}{FAIL} failed{NC}")
    print("=" * 50)

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
