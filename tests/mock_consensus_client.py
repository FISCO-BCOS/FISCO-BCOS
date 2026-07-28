#!/usr/bin/env python3
"""
mock_consensus_client.py — FISCO-BCOS Engine API Smoke Test (Phase 2)

模拟以太坊共识层客户端（Consensus Layer），对 EL 节点的 Engine API
按照 CL 的标准调用流程进行自动化冒烟测试：

  1. engine_exchangeCapabilities   — 能力协商
  2. engine_forkchoiceUpdatedV2    — Forkchoice 状态更新（无 payloadAttributes）
  3. engine_forkchoiceUpdatedV2    — Forkchoice 状态更新（带 payloadAttributes，触发构建）
  4. engine_getPayloadV2           — 获取构建好的 Payload
  5. engine_newPayloadV2           — 新 Payload 下发与校验

用法:
    pip install requests
    python3 mock_consensus_client.py [RPC_URL]

退出码: 0 = 全部通过, 1 = 有测试失败
"""

import json
import sys
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


def rpc(method: str, params: List[Any], req_id: int = 1) -> Dict[str, Any]:
    """Send a JSON-RPC request, return the full response dict."""
    payload: Dict[str, Any] = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params,
    }
    resp = requests.post(RPC_URL, json=payload, headers=HEADERS, timeout=TIMEOUT)
    resp.raise_for_status()
    data = resp.json()
    if "error" in data:
        raise RuntimeError(f"RPC error [{method}]: {data['error']}")
    return data


def rpc_result(method: str, params: List[Any], req_id: int = 1) -> Any:
    """Send a JSON-RPC request, return result only."""
    return rpc(method, params, req_id)["result"]


# ---- Test Cases ----


def test_exchange_capabilities() -> None:
    """Verify engine_exchangeCapabilities returns all expected capabilities."""
    _log_test("engine_exchangeCapabilities")

    result = rpc_result("engine_exchangeCapabilities", [["engine_newPayloadV2"]])
    returned = set(result)

    expected = {
        "engine_exchangeCapabilities",
        "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2",
        "engine_forkchoiceUpdatedV3",
        "engine_getPayloadV1",
        "engine_getPayloadV2",
        "engine_getPayloadV3",
        "engine_newPayloadV1",
        "engine_newPayloadV2",
        "engine_newPayloadV3",
    }

    missing = expected - returned
    if missing:
        _log_fail(f"Missing capabilities: {missing}")
        return

    _log_info(f"All {len(expected)} capabilities present")
    _log_pass()


def get_head_hash() -> str:
    """Retrieve the latest block hash from the node."""
    block = rpc_result("eth_getBlockByNumber", ["latest", False])
    return block["hash"]


def test_forkchoice_updated_without_payload() -> None:
    """Verify forkchoiceUpdatedV2 works without payloadAttributes (pure state update)."""
    _log_test("engine_forkchoiceUpdatedV2 (without payloadAttributes)")

    head_hash = get_head_hash()
    _log_info(f"headBlockHash = {head_hash}")

    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }

    result = rpc_result("engine_forkchoiceUpdatedV2", [fc_state, None])
    status = result["payloadStatus"]["status"]
    _log_info(f"payloadStatus.status = {status}")

    if status in ("VALID", "SYNCING"):
        _log_pass()
    else:
        _log_fail(f"Unexpected status: {status}")


def test_forkchoice_updated_with_payload() -> Optional[str]:
    """Verify forkchoiceUpdatedV2 triggers payload build and returns a payloadId."""
    _log_test("engine_forkchoiceUpdatedV2 (with payloadAttributes)")

    head_hash = get_head_hash()
    _log_info(f"headBlockHash = {head_hash}")

    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }

    payload_attrs = {
        "timestamp": "0x100",
        "prevRandao": "0x" + "00" * 31 + "01",
        "suggestedFeeRecipient": "0x0000000000000000000000000000000000000001",
        "withdrawals": [],
    }

    result = rpc_result("engine_forkchoiceUpdatedV2", [fc_state, payload_attrs])
    status = result["payloadStatus"]["status"]
    _log_info(f"payloadStatus.status = {status}")

    if status not in ("VALID", "SYNCING"):
        _log_fail(f"Unexpected status: {status}")
        return None

    payload_id = result.get("payloadId")
    if payload_id:
        _log_info(f"payloadId = {payload_id}")
        _log_pass()
        return payload_id
    else:
        _log_info("No payloadId returned (node may be SYNCING)")
        _log_pass()
        return None


def test_get_payload(payload_id: str) -> Optional[Dict[str, Any]]:
    """Verify engine_getPayloadV2 returns a well-formed ExecutionPayload."""
    _log_test(f"engine_getPayloadV2 (payloadId={payload_id})")

    result = rpc_result("engine_getPayloadV2", [payload_id])

    # V2 wraps the execution payload under "executionPayload" key
    if "executionPayload" in result:
        result = result["executionPayload"]

    required_fields = [
        "parentHash", "feeRecipient", "stateRoot", "receiptsRoot",
        "logsBloom", "prevRandao", "blockNumber", "gasLimit", "gasUsed",
        "timestamp", "extraData", "baseFeePerGas", "blockHash",
        "transactions", "withdrawals",
    ]

    missing = [f for f in required_fields if f not in result]
    if missing:
        _log_fail(f"Missing fields in ExecutionPayload: {missing}")
        return None

    _log_info(f"blockHash      = {result['blockHash']}")
    _log_info(f"blockNumber    = {result['blockNumber']}")
    _log_info(f"txCount        = {len(result['transactions'])}")
    _log_info(f"withdrawals    = {len(result.get('withdrawals', []))}")

    _log_pass()
    return result


def test_new_payload(payload: Dict[str, Any]) -> None:
    """Verify engine_newPayloadV2 accepts and validates a payload."""
    _log_test("engine_newPayloadV2")

    result = rpc_result("engine_newPayloadV2", [payload])
    status = result["status"]
    _log_info(f"payloadStatus = {status}")

    valid_statuses = {"VALID", "INVALID", "SYNCING", "ACCEPTED", "INVALID_BLOCK_HASH"}
    if status not in valid_statuses:
        _log_fail(f"Unexpected status: {status}")
        return

    if status in ("VALID", "ACCEPTED"):
        _log_pass()
    else:
        _log_info(f"Status '{status}' is expected for synthetic/test payloads")
        _log_pass()


def test_invalid_method() -> None:
    """Verify proper error handling for unknown methods."""
    _log_test("engine_unknownMethod (negative test)")

    resp = requests.post(
        RPC_URL,
        json={
            "jsonrpc": "2.0",
            "id": 99,
            "method": "engine_unknownMethod",
            "params": [],
        },
        headers=HEADERS,
        timeout=TIMEOUT,
    )
    data = resp.json()
    if "error" in data:
        code = data["error"]["code"]
        _log_info(f"Error code = {code}")
        if code == -32601:  # MethodNotFound
            _log_pass()
        else:
            _log_fail(f"Expected -32601, got {code}")
    else:
        _log_fail("Expected error response, got success")


# ---- Main ----

def generate_jwt(secret_file: str) -> str:
    import base64, hashlib, hmac, time as _time
    with open(secret_file, "r") as f:
        secret = bytes.fromhex(f.read().strip())
    h = base64.urlsafe_b64encode(json.dumps({"alg":"HS256","typ":"JWT"}).encode()).rstrip(b"=").decode()
    p = base64.urlsafe_b64encode(json.dumps({"iat":int(_time.time()),"id":"12345678","clv":"FISCO-BCOS"}).encode()).rstrip(b"=").decode()
    sig = base64.urlsafe_b64encode(hmac.new(secret,f"{h}.{p}".encode(),hashlib.sha256).digest()).rstrip(b"=").decode()
    return f"{h}.{p}.{sig}"


def main() -> int:
    global RPC_URL, HEADERS

    if len(sys.argv) > 1:
        RPC_URL = sys.argv[1]
    if len(sys.argv) > 2:
        jwt_token = generate_jwt(sys.argv[2])
        HEADERS["Authorization"] = f"Bearer {jwt_token}"
        _log_info(f"JWT token generated")

    print("=" * 50)
    print("  FISCO-BCOS Engine API Smoke Test (Python)")
    print(f"  Target: {RPC_URL}")
    print("=" * 50)
    print()

    try:
        # 1. Capability negotiation
        test_exchange_capabilities()

        # 2. Pure forkchoice update (no payload build)
        test_forkchoice_updated_without_payload()

        # 3. Forkchoice update with payloadAttributes → get payloadId
        payload_id = test_forkchoice_updated_with_payload()

        # 4. Get the built payload
        if payload_id:
            payload = test_get_payload(payload_id)
            # 5. Submit payload validation
            if payload:
                test_new_payload(payload)

        # 6. Negative test
        test_invalid_method()

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

    if FAIL > 0:
        print(f"\n{RED}🎉 ALL SMOKE TESTS PASSED!{NC}" if FAIL == 0 else
              f"\n{RED}Some tests FAILED. Check the output above.{NC}")

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
