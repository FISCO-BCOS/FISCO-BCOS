#!/usr/bin/env python3
"""
mock_consensus_client.py — FISCO-BCOS Engine API Smoke Test (Karst dialect)

模拟 op-node 对 EL 节点的 Engine API 调用流程(W0 mock,B4 起说 Karst 方言):

  1. engine_exchangeCapabilities   — 节点实现的全部方法(Karst 三件套 + V1-V3 旧版本)
  2. engine_forkchoiceUpdatedV3    — 无 payloadAttributes(纯状态更新)
  3. engine_forkchoiceUpdatedV3    — 带 payloadAttributes:秒级时间戳、注入一笔 0x7e
                                     deposit 裸交易、noTxPool true/false 轮换
  4. engine_getPayloadV5           — 校验 V5 响应形状,断言 deposit 原字节在列
  5. engine_newPayloadV4           — [payload, [], beaconRoot, []] 提交
  6. V2 旧版本回路                 — FCU V2 -> getPayloadV2 -> newPayloadV2 仍然可用
  7. 负测                          — 未实现版本 -38005、未知 payloadId -38001、缺参 -32602

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
# NOT narrowed to the active fork: op-geth advertises its whole method set and lets the CL
# pick, and the pre-Karst versions stay served.
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
}

# The one genuinely unimplemented version: routable, answers -38005, never advertised.
UNIMPLEMENTED_METHODS = ("engine_forkchoiceUpdatedV4",)

# The gas limit sent in payloadAttributes. See test_negative_cases / the note below: this
# node currently IGNORES it and takes the gas limit from its own SystemConfig.
ATTRS_GAS_LIMIT = "0x1c9c380"

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
    """The node advertises everything it implements, pre-Karst versions included."""
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
    """Engine wire timestamps are Unix SECONDS (execution-apis; op-node sends seconds)."""
    global _timestamp_bump
    _timestamp_bump += 1
    return hex(int(time.time()) + _timestamp_bump)


def run_karst_block_flow(no_tx_pool: bool) -> bool:
    """One op-node-shaped block: FCU V3 (deposit injected) -> getPayloadV5 -> newPayloadV4."""
    label = f"noTxPool={str(no_tx_pool).lower()}"
    _log_test(f"FCU V3 + getPayloadV5 + newPayloadV4 ({label})")

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
        "transactions": [DEPOSIT_RAW],
        "noTxPool": no_tx_pool,
        "gasLimit": ATTRS_GAS_LIMIT,
        # Bare JSON number, NOT a hex string: op-node serializes MinBaseFee as a plain
        # *uint64 without hexutil (op-service/eth/types.go:523, v1.19.3), so the mock
        # must put the same wire shape on the wire ("minBaseFee": 0).
        "minBaseFee": 0,
        # Holocene Bytes8 wire shape: an 8-byte hex string, u32 BE denominator in
        # [0:4] and u32 BE elasticity in [4:8] (op-service/eth/types.go:521,
        # EIP1559Params *Bytes8, v1.19.3). All-zero means "SystemConfig has not set
        # the params"; the EL translates 0,0 to the Canyon constants (250, 6) in the
        # built extraData.
        "eip1559Params": "0x0000000000000000",
    }

    fcu = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, payload_attrs])
    status = fcu["payloadStatus"]["status"]
    payload_id = fcu.get("payloadId")
    _log_info(f"FCU status={status}, payloadId={payload_id}")
    if status != "VALID" or not payload_id:
        _log_fail(f"FCU did not build a payload: status={status}, payloadId={payload_id}")
        return False

    result = rpc_result("engine_getPayloadV5", [payload_id])

    # V5 response shape (op-node expectation).
    for field in (
        "executionPayload",
        "blockValue",
        "blobsBundle",
        "shouldOverrideBuilder",
        "executionRequests",
    ):
        if field not in result:
            _log_fail(f"getPayloadV5 response missing '{field}': {sorted(result.keys())}")
            return False
    bundle = result["blobsBundle"]
    if any(bundle.get(k) != [] for k in ("commitments", "proofs", "blobs")):
        _log_fail(f"blobsBundle must be three empty arrays, got {bundle}")
        return False
    if result["executionRequests"] != [] or result["shouldOverrideBuilder"] is not False:
        _log_fail("executionRequests must be [] and shouldOverrideBuilder false")
        return False

    payload = result["executionPayload"]
    if "withdrawalsRoot" not in payload:
        _log_fail("executionPayload missing withdrawalsRoot (Isthmus V4 shape)")
        return False

    # Unit check, wire side: the Engine boundary speaks Unix SECONDS in both directions.
    # This catches a ONE-SIDED break only (parse or serialize, not both): removing the
    # conversion from both cancels out on the wire and leaves this equality holding. The
    # symmetric case is caught by the committed-block check further down, which reads the
    # header through eth_getBlockByNumber.
    got_seconds = int(payload["timestamp"], 16)
    if abs(got_seconds - sent_seconds) > 1:
        _log_fail(
            f"getPayloadV5 timestamp is not the Unix seconds we sent: "
            f"sent {sent_seconds}, got {got_seconds}"
        )
        return False

    # dep-1 byte fidelity: the injected deposit's raw bytes lead the transaction list.
    txs = payload["transactions"]
    if not txs or txs[0].lower() != DEPOSIT_RAW.lower():
        _log_fail(f"deposit raw bytes not first in payload transactions: {txs[:2]}")
        return False
    if no_tx_pool and len(txs) != 1:
        _log_fail(f"noTxPool=true payload must contain exactly the forced deposit, got {len(txs)}")
        return False
    _log_info(f"deposit in payload at index 0 ({len(txs)} tx total)")

    beacon_root = result.get("parentBeaconBlockRoot", ZERO_HASH)
    new_status = rpc_result("engine_newPayloadV4", [payload, [], beacon_root, []])
    # VALID only. ACCEPTED would mean the node acknowledged the block without validating
    # or storing it, which is exactly the escape M9 removed — accepting it here would let
    # that regression back in silently.
    _log_info(f"newPayloadV4 status = {new_status['status']}")
    if new_status["status"] != "VALID":
        _log_fail(f"newPayloadV4 did not answer VALID for the built payload: {new_status}")
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


def run_v2_block_flow() -> None:
    """The pre-Karst loop still works end to end: FCU V2 -> getPayloadV2 -> newPayloadV2.

    Targeting Karst does not make the older method versions incompatible. A stock CL
    driving the standard V1-V3 surface (and the v1 Engine API harness kept alive by
    unsafe_allow_v1_executor) must keep building and submitting blocks.
    """
    _log_test("FCU V2 + getPayloadV2 + newPayloadV2 (pre-Karst surface)")

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

    fcu = rpc_result("engine_forkchoiceUpdatedV2", [fc_state, payload_attrs])
    payload_id = fcu.get("payloadId")
    if fcu["payloadStatus"]["status"] != "VALID" or not payload_id:
        _log_fail(f"FCU V2 did not build a payload: {fcu}")
        return

    result = rpc_result("engine_getPayloadV2", [payload_id])
    # V2 response shape: executionPayload + blockValue only — no blobsBundle (V3+),
    # no executionRequests (V4+), no withdrawalsRoot on the payload (V4+).
    if "executionPayload" not in result or "blockValue" not in result:
        _log_fail(f"getPayloadV2 response is not the V2 shape: {sorted(result.keys())}")
        return
    if "blobsBundle" in result or "executionRequests" in result:
        _log_fail(f"getPayloadV2 leaked V3+/V4+ response fields: {sorted(result.keys())}")
        return
    payload = result["executionPayload"]
    if "withdrawalsRoot" in payload:
        _log_fail("getPayloadV2 leaked the V4-only withdrawalsRoot field")
        return

    status = rpc_result("engine_newPayloadV2", [payload])
    _log_info(f"newPayloadV2 status = {status['status']}")
    if status["status"] != "VALID":
        _log_fail(f"newPayloadV2 rejected the V2 payload it just built: {status}")
        return
    _log_pass()


def test_eip1559_fields_are_v3_only() -> None:
    """eip1559Params / minBaseFee must be refused on a pre-Holocene FCU version.

    op-node only ever puts these on a forkchoiceUpdatedV3: Config.ForkchoiceUpdatedVersion
    (op-node/rollup/types.go:727-745, v1.19.3) answers FCUV3 from Ecotone onwards, FCUV2
    for Canyon and FCUV1 before it, and there is no FCUV4 — while Holocene (eip1559Params)
    and Jovian (minBaseFee) both activate after Ecotone. If a V1/V2 FCU carrying them were
    accepted, the build would stamp Holocene/Jovian extraData on a pre-Holocene block and
    a spec-conformant CL would reject the header on read-back ("extraData must be empty
    before Holocene", op-core/eip1559/eip1559.go:27-28).

    Attribute errors surface through the payloadStatus channel, not as a JSON-RPC error.
    """
    _log_test("eip1559Params / minBaseFee are rejected below forkchoiceUpdatedV3")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }

    def v2_attrs(**extra: object) -> dict:
        attrs = {
            "timestamp": next_timestamp(),
            "prevRandao": PREV_RANDAO,
            "suggestedFeeRecipient": FEE_RECIPIENT,
            "withdrawals": [],
        }
        attrs.update(extra)
        return attrs

    # Control: the same attributes without the two fields still build on V2, so a
    # rejection below is attributable to the new gate and not to an unrelated V2 error.
    control = rpc_result("engine_forkchoiceUpdatedV2", [fc_state, v2_attrs()])
    if control["payloadStatus"]["status"] != "VALID" or not control.get("payloadId"):
        _log_fail(f"control V2 FCU did not build a payload: {control}")
        return

    for field, value in (
        ("eip1559Params", "0x000000fa00000006"),
        ("minBaseFee", 0),
    ):
        fcu = rpc_result("engine_forkchoiceUpdatedV2", [fc_state, v2_attrs(**{field: value})])
        status = fcu["payloadStatus"]["status"]
        if status != "INVALID" or fcu.get("payloadId"):
            _log_fail(
                f"forkchoiceUpdatedV2 accepted {field}: status={status}, "
                f"payloadId={fcu.get('payloadId')}"
            )
            return
        _log_info(f"V2 + {field} -> INVALID ({fcu['payloadStatus'].get('validationError')})")

    _log_pass()


def test_new_payload_rejects_tampered_extra_data() -> None:
    """newPayload must not wave through a payload whose extraData was altered.

    extraData is a block-hash input from this change onwards, so a CL returning a
    payload under the blockHash it was handed must return the same extraData. op-geth
    catches this by re-deriving the block hash from the payload fields it received
    (beacon/engine/types.go:287-288); this node cannot re-derive an Ethereum block hash,
    so it compares against the payload it handed out. A malformed shape is rejected too,
    mirroring op-geth's header verification (consensus/beacon/consensus.go:240-243).
    """
    _log_test("newPayload rejects altered / malformed extraData")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }
    attrs = {
        "timestamp": next_timestamp(),
        "prevRandao": PREV_RANDAO,
        "suggestedFeeRecipient": FEE_RECIPIENT,
        "withdrawals": [],
        "parentBeaconBlockRoot": ZERO_HASH,
        "noTxPool": True,
        "minBaseFee": 0,
        "eip1559Params": "0x0000000000000000",
    }

    fcu = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, attrs])
    payload_id = fcu.get("payloadId")
    if not payload_id:
        _log_fail(f"FCU built no payload: {fcu}")
        return
    payload = rpc_result("engine_getPayloadV3", [payload_id])["executionPayload"]

    # The build must have stamped the 17-byte Jovian form (0,0 -> Canyon 250/6).
    if payload["extraData"].lower() != "0x01000000fa000000060000000000000000":
        _log_fail(f"built extraData is not the Jovian vector: {payload['extraData']}")
        return

    for label, extra_data in (
        # Same shape, different minBaseFee byte: this is the built-payload comparison.
        ("altered", "0x01000000fa000000060000000000000009"),
        # 17 bytes carrying the Holocene version byte: this is the shape gate.
        ("malformed", "0x00000000fa000000060000000000000000"),
        # Zero denominator/elasticity is illegal on a header (legal only in attributes).
        ("zero-params", "0x010000000000000000" + "00" * 8),
    ):
        tampered = dict(payload, extraData=extra_data)
        status = rpc_result("engine_newPayloadV3", [tampered, [], ZERO_HASH])
        if status["status"] == "VALID":
            _log_fail(f"newPayload accepted {label} extraData {extra_data}")
            return
        _log_info(f"{label} -> {status['status']} ({status.get('validationError')})")

    # Control: the untouched payload still commits, so the checks above are not simply
    # rejecting everything.
    status = rpc_result("engine_newPayloadV3", [payload, [], ZERO_HASH])
    if status["status"] != "VALID":
        _log_fail(f"newPayload rejected the untouched payload it built: {status}")
        return
    _log_info("untouched payload -> VALID")
    _log_pass()


def test_attributes_gas_limit_is_ignored() -> None:
    """KNOWN GAP pin: payloadAttributes.gasLimit does not reach the built block.

    On OP Stack the CL relays the L1 SystemConfig gas limit in this attribute and op-geth
    honours it verbatim (miner/worker.go prepareWork), rejecting the FCU outright when it
    is absent (api_optimism.go checkOptimismPayloadAttributes). This node instead always
    takes the gas limit from its own SystemConfig, so two builds that differ ONLY in the
    attribute come out with the same gasLimit. Wiring the attribute in changes block
    production and is left to the header-fields work; this check turns red the day it
    happens, so the gap cannot be forgotten.
    """
    _log_test("payloadAttributes.gasLimit is ignored (known gap)")

    head_hash = get_head_hash()
    fc_state = {
        "headBlockHash": head_hash,
        "safeBlockHash": head_hash,
        "finalizedBlockHash": head_hash,
    }

    def build_with(gas_limit: str) -> str:
        attrs = {
            "timestamp": next_timestamp(),
            "prevRandao": PREV_RANDAO,
            "suggestedFeeRecipient": FEE_RECIPIENT,
            "withdrawals": [],
            "parentBeaconBlockRoot": ZERO_HASH,
            "noTxPool": True,
            "gasLimit": gas_limit,
        }
        fcu = rpc_result("engine_forkchoiceUpdatedV3", [fc_state, attrs])
        payload_id = fcu.get("payloadId")
        if not payload_id:
            raise RuntimeError(f"FCU built no payload: {fcu}")
        result = rpc_result("engine_getPayloadV5", [payload_id])
        return result["executionPayload"]["gasLimit"]

    first = build_with(ATTRS_GAS_LIMIT)
    second = build_with("0x2faf080")
    if first != second:
        _log_fail(
            "payloadAttributes.gasLimit now affects the built block "
            f"({first} vs {second}) — the known gap is closed, update this test"
        )
        return
    _log_info(f"built gasLimit = {first} for both attribute values (from SystemConfig)")
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
    global RPC_URL, HEADERS

    if len(sys.argv) > 1:
        RPC_URL = sys.argv[1]
    if len(sys.argv) > 2:
        jwt_token = generate_jwt(sys.argv[2])
        HEADERS["Authorization"] = f"Bearer {jwt_token}"
        _log_info("JWT token generated")

    print("=" * 50)
    print("  FISCO-BCOS Engine API Smoke Test (Karst dialect)")
    print(f"  Target: {RPC_URL}")
    print("=" * 50)
    print()

    try:
        # 1. Capability negotiation: Karst-only surface.
        test_exchange_capabilities()

        # 2. Pure forkchoice update (no payload build).
        test_forkchoice_v3_without_payload()

        # 3./4./5. Two full block flows with noTxPool rotation: the forced deposit must
        # be the whole payload under noTxPool=true, and still lead it under false.
        if run_karst_block_flow(no_tx_pool=True):
            run_karst_block_flow(no_tx_pool=False)

        # 6. The pre-Karst surface is still live.
        run_v2_block_flow()

        # 7. Holocene/Jovian attribute fields are forkchoiceUpdatedV3-only.
        test_eip1559_fields_are_v3_only()

        # 8. Commit-side extraData gates: altered / malformed shapes are not VALID.
        test_new_payload_rejects_tampered_extra_data()

        # 9. Known gap: attributes.gasLimit does not reach the built block.
        test_attributes_gas_limit_is_ignored()

        # 10. Error semantics.
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
