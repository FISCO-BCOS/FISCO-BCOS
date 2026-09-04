/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

// Review fixes for PR #5544. Oracles:
//   - extraData 0,0 -> Canyon 250/6: op-core/eip1559 EncodeHoloceneExtraData
//     (specs holocene exec-engine); byte literal 0x00000000fa00000006
//   - GetPayloadV4 accepts only PayloadV3: op-geth payloadID.Is(PayloadV3)
//   - FCU V3/V4 attrs require BeaconRoot: op-geth ForkchoiceUpdatedV3/V4
//   - leftover RLP after decodeTyped is invalid
//   - blob (0x03) rejection is FISCO OP policy, not an op-geth decodeTyped check

#include "engine/bcos-engine/EngineServiceCommon.h"
#include "engine/bcos-engine/OpEngineService.h"
#include "engine/bcos-engine/PayloadCache.h"
#include "support/GoldenSample.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <opstack-executor/tests/OpSchedulerSeamTestHelpers.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::engine;

namespace
{
PayloadAttributes holoceneAttributes(bytes eip1559Params)
{
    PayloadAttributes attributes;
    attributes.timestamp = 1;
    attributes.withdrawals = std::vector<WithdrawalV1>{};
    attributes.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    attributes.eip1559Params = std::move(eip1559Params);
    attributes.gasLimit = 30'000'000;
    return attributes;
}

NewPayloadRequest makeIsthmusNewPayload(bytes extraData)
{
    NewPayloadRequest request;
    request.executionPayload.withdrawals = std::vector<WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = h256(1);
    request.executionPayload.excessBlobGas = u256(0);
    request.executionPayload.blobGasUsed = u256(0);
    request.executionPayload.gasLimit = u256(30'000'000);
    request.executionPayload.gasUsed = u256(0);
    request.executionPayload.blockNumber = 1;
    request.executionPayload.extraData = std::move(extraData);
    request.parentBeaconBlockRoot = h256(2);
    return request;
}

Json::Value hex32(char fill)
{
    return Json::Value("0x" + std::string(64, fill));
}

Json::Value hex20(char fill)
{
    return Json::Value("0x" + std::string(40, fill));
}

// Minimal `_op_payload` that reaches parseNewPayloadRequest field readers. `nullField`
// is left as JSON null so makeInvalidParamsJson must not strip it (finding E6).
w6test::InvalidSample invalidSampleWithNullField(char const* nullField)
{
    w6test::InvalidSample sample;
    Json::Value op(Json::objectValue);
    op["parentHash"] = hex32('1');
    op["stateRoot"] = hex32('2');
    op["receiptsRoot"] = hex32('3');
    op["prevRandao"] = hex32('4');
    op["blockHash"] = hex32('5');
    op["feeRecipient"] = hex20('6');
    op["gasLimit"] = "0x1";
    op["gasUsed"] = "0x0";
    op["baseFeePerGas"] = "0x0";
    op["timestamp"] = "0x1";
    op["blockNumber"] = "0x1";
    op["withdrawals"] = Json::Value(Json::arrayValue);
    op["transactions"] = Json::Value(Json::arrayValue);
    op["blobGasUsed"] = "0x0";
    op["excessBlobGas"] = "0x0";
    op["withdrawalsRoot"] = hex32('0');
    op["parentBeaconBlockRoot"] = hex32('7');
    op[nullField] = Json::Value(Json::nullValue);
    sample.vector["_op_payload"] = std::move(op);
    return sample;
}

void checkParseRejects(
    Json::Value const& params, bcos::engine::ApiVersion version, int32_t code, char const* message)
{
    BOOST_CHECK_EXCEPTION(bcos::rpc::parseNewPayloadRequest(params, version),
        bcos::rpc::JsonRpcException, [&](bcos::rpc::JsonRpcException const& e) {
            return e.code() == code && e.msg() == message;
        });
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineReviewFixTest)

BOOST_AUTO_TEST_CASE(holocene_zero_params_encode_canyon_and_pass_newpayload_gate)
{
    // op-node may send eip1559Params = 0x0000000000000000; the EL must stamp Canyon
    // 250/6 into header extraData. Header extraData itself must stay non-zero.
    auto extraData = engine::detail::encodeOptimismExtraData(holoceneAttributes(bytes(8, 0)));
    BOOST_REQUIRE_EQUAL(toHexStringWithPrefix(extraData), "0x00000000fa00000006");
    BOOST_CHECK(!engine_common::op::validateOpNewPayloadRequest(
        makeIsthmusNewPayload(extraData), /*jovianActive=*/false));
}

BOOST_AUTO_TEST_CASE(holocene_header_extra_data_matches_validate_holocene_1559_params)
{
    // Committed headers must carry a usable (non-zero) EIP-1559 pair - the same rule
    // calcOpBaseFee enforces on the parent (finding AO). The lenient attribute-side
    // 0,0 acceptance does NOT extend to headers: a zero-elasticity or zero-denominator
    // head can never be extended by the next FCU build / child newPayload.
    auto accept = [](bytes extra, bool jovian) {
        return !engine_common::op::validateOpNewPayloadRequest(
            makeIsthmusNewPayload(std::move(extra)), jovian);
    };
    auto rejectContains = [](bytes extra, bool jovian, std::string_view needle) {
        auto error = engine_common::op::validateOpNewPayloadRequest(
            makeIsthmusNewPayload(std::move(extra)), jovian);
        BOOST_REQUIRE(error.has_value());
        BOOST_CHECK(error->find(std::string(needle)) != std::string::npos);
    };

    BOOST_CHECK(accept(fromHex("00000000fa00000006"), false));
    rejectContains(fromHex("000000000000000000"), false, "non-zero EIP-1559");
    rejectContains(fromHex("00000000fa00000000"), false, "non-zero EIP-1559");
    rejectContains(fromHex("000000000000000006"), false, "non-zero EIP-1559");
}

BOOST_AUTO_TEST_CASE(op_newpayload_rejects_extradata_length_and_version)
{
    auto rejectContains = [](bytes extra, bool jovian, std::string_view needle) {
        auto error = engine_common::op::validateOpNewPayloadRequest(
            makeIsthmusNewPayload(std::move(extra)), jovian);
        BOOST_REQUIRE(error.has_value());
        BOOST_CHECK(error->find(std::string(needle)) != std::string::npos);
    };

    rejectContains(fromHex("00000000fa000000"), false, "exactly 9 bytes");
    rejectContains(fromHex("01000000fa00000006"), false, "0x00");
    rejectContains(fromHex("00000000fa00000006"), true, "exactly 17 bytes");
    rejectContains(fromHex("00000000fa000000060000000000000000"), true, "0x01");
}

BOOST_AUTO_TEST_CASE(attrs_holocene_pairing_matches_op_geth)
{
    auto zeroDenom = holoceneAttributes(fromHex("0000000000000006"));
    auto zeroElasticity = holoceneAttributes(fromHex("000000fa00000000"));
    auto bothZero = holoceneAttributes(bytes(8, 0));

    auto commonZeroDenom = engine_common::validatePayloadAttributes(zeroDenom, 3);
    BOOST_REQUIRE(commonZeroDenom.has_value());
    BOOST_CHECK(commonZeroDenom->find("both zero or both non-zero") != std::string::npos);
    // (d>0,e==0) is rejected too: encode writes it verbatim and calcOpBaseFee cannot
    // extend a zero-elasticity head (finding AO).
    BOOST_REQUIRE(engine_common::validatePayloadAttributes(zeroElasticity, 3).has_value());
    BOOST_CHECK(!engine_common::validatePayloadAttributes(bothZero, 3));

    BOOST_REQUIRE(engine_common::op::validateOpPayloadAttributes(zeroDenom, false).has_value());
    BOOST_REQUIRE(
        engine_common::op::validateOpPayloadAttributes(zeroElasticity, false).has_value());
    BOOST_CHECK(!engine_common::op::validateOpPayloadAttributes(bothZero, false));
}

BOOST_AUTO_TEST_CASE(op_does_not_advertise_unimplemented_fcu_v4)
{
    auto caps = engine_common::op::supportedOpCapabilities();
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_forkchoiceUpdatedV3") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_forkchoiceUpdatedV4") == caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_newPayloadV4") != caps.end());
}

BOOST_AUTO_TEST_CASE(newpayload_rejects_blob_type_as_fisco_op_policy)
{
    auto request = makeIsthmusNewPayload(fromHex("00000000fa00000006"));
    request.executionPayload.transactions.push_back(
        EngineTransaction{.raw = bytes{0x03, 0xaa}, .decoded = nullptr});
    auto error = engine_common::op::validateOpNewPayloadRequest(request, /*jovianActive=*/false);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK(error->find("blob transactions are not allowed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(fcu_v4_missing_beacon_root_is_invalid)
{
    // op-geth ForkchoiceUpdatedV4: attrs present => BeaconRoot required.
    auto attributes = holoceneAttributes(bytes(8, 0));
    attributes.parentBeaconBlockRoot = std::nullopt;
    auto error = engine_common::validatePayloadAttributes(attributes, 4);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK(error->find("parentBeaconBlockRoot") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(payload_shape_version_v4_is_payload_v3)
{
    // op-geth: ForkchoiceUpdatedV3/V4 store PayloadV3; GetPayloadV4 requires PayloadV3.
    BOOST_CHECK_EQUAL(engine_common::payloadShapeVersion(3), 3);
    BOOST_CHECK_EQUAL(engine_common::payloadShapeVersion(4), 3);
    BOOST_CHECK(engine_common::isGetPayloadVersionCompatible(ApiVersion::V4, 3));
    BOOST_CHECK(!engine_common::isGetPayloadVersionCompatible(ApiVersion::V4, 4));
    BOOST_CHECK(engine_common::isGetPayloadVersionCompatible(ApiVersion::V5, 3));
}

BOOST_AUTO_TEST_CASE(op_envelope_to_tars_rejects_rlp_leftover)
{
    auto env = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
    auto hash = crypto::keccak256Hash(bcos::ref(env));
    BOOST_REQUIRE(engine_common::op::opEnvelopeToTars(env, hash).has_value());
    env.push_back(0x00);
    BOOST_CHECK(!engine_common::op::opEnvelopeToTars(env, hash).has_value());
}

BOOST_AUTO_TEST_CASE(forkchoice_hash_canonical_helper_matches_op_geth)
{
    // op-geth ReadCanonicalHash(number) != hash → not canonical.
    // Missing NUMBER_2_HASH is fail-closed (not canonical).
    BOOST_CHECK(!engine_common::forkchoiceHashIsCanonical(h256(1), std::nullopt));
    BOOST_CHECK(engine_common::forkchoiceHashIsCanonical(h256(1), h256(1)));
    BOOST_CHECK(!engine_common::forkchoiceHashIsCanonical(h256(1), h256(2)));
}

BOOST_AUTO_TEST_CASE(l1_attributes_deposit_required_when_synthesis_disabled)
{
    // op-geth miner.BuildPayload never synthesizes an L1-attributes deposit.
    auto empty = holoceneAttributes(bytes(8, 0));
    auto missing = engine_common::op::requireL1AttributesDeposit(empty, false);
    BOOST_REQUIRE(missing.has_value());
    BOOST_CHECK(missing->find("L1 attributes deposit") != std::string::npos);

    empty.transactions = std::vector<std::string>{};
    auto emptyList = engine_common::op::requireL1AttributesDeposit(empty, false);
    BOOST_REQUIRE(emptyList.has_value());

    auto phaseA = holoceneAttributes(bytes(8, 0));
    BOOST_CHECK(!engine_common::op::requireL1AttributesDeposit(phaseA, true));

    auto withDeposit = holoceneAttributes(bytes(8, 0));
    withDeposit.transactions = std::vector<std::string>{"0x7e00"};
    BOOST_CHECK(!engine_common::op::requireL1AttributesDeposit(withDeposit, false));
}

BOOST_AUTO_TEST_CASE(invalid_params_json_keeps_null_blob_fields)
{
    // Matrix: E6 — helper must not strip JSON null before parse sees it.
    auto sample = invalidSampleWithNullField("blobGasUsed");
    auto params = w6test::makeInvalidParamsJson(sample);
    BOOST_REQUIRE(params[0u].isMember("blobGasUsed"));
    BOOST_CHECK(params[0u]["blobGasUsed"].isNull());
    BOOST_REQUIRE(params[0u].isMember("excessBlobGas"));
    BOOST_CHECK(params[0u]["excessBlobGas"].isString());

    auto excessNull = invalidSampleWithNullField("excessBlobGas");
    auto excessParams = w6test::makeInvalidParamsJson(excessNull);
    BOOST_REQUIRE(excessParams[0u].isMember("excessBlobGas"));
    BOOST_CHECK(excessParams[0u]["excessBlobGas"].isNull());
}

BOOST_AUTO_TEST_CASE(parse_rejects_json_null_blob_fields)
{
    // Matrix: E6 — wire-null blob fields are -32602, not "absent then Engine INVALID".
    auto blobParams = w6test::makeInvalidParamsJson(invalidSampleWithNullField("blobGasUsed"));
    checkParseRejects(blobParams, bcos::engine::ApiVersion::V4, bcos::rpc::InvalidParams,
        "executionPayload.blobGasUsed must be a hex string for ExecutionPayloadV4");
    checkParseRejects(blobParams, bcos::engine::ApiVersion::V3, bcos::rpc::InvalidParams,
        "executionPayload.blobGasUsed must be a hex quantity string of at most 32 bytes");

    auto excessParams = w6test::makeInvalidParamsJson(invalidSampleWithNullField("excessBlobGas"));
    checkParseRejects(excessParams, bcos::engine::ApiVersion::V4, bcos::rpc::InvalidParams,
        "executionPayload.excessBlobGas must be a hex string for ExecutionPayloadV4");
    checkParseRejects(excessParams, bcos::engine::ApiVersion::V3, bcos::rpc::InvalidParams,
        "executionPayload.excessBlobGas must be a hex quantity string of at most 32 bytes");
}

BOOST_AUTO_TEST_CASE(op_newpayload_rejects_missing_blob_fields_and_wide_gas_limit)
{
    // Matrix: E6 — OP static face, not golden-stripped JSON.
    auto const extra = fromHex("00000000fa00000006");
    auto base = makeIsthmusNewPayload(extra);

    auto missingBlob = base;
    missingBlob.executionPayload.blobGasUsed = std::nullopt;
    auto missingBlobError =
        engine_common::op::validateOpNewPayloadRequest(missingBlob, /*jovianActive=*/false);
    BOOST_REQUIRE(missingBlobError.has_value());
    BOOST_CHECK_EQUAL(*missingBlobError, "blobGasUsed must be present on the OP path");

    auto missingExcess = base;
    missingExcess.executionPayload.excessBlobGas = std::nullopt;
    auto missingExcessError =
        engine_common::op::validateOpNewPayloadRequest(missingExcess, /*jovianActive=*/false);
    BOOST_REQUIRE(missingExcessError.has_value());
    BOOST_CHECK_EQUAL(*missingExcessError, "excessBlobGas must be present and zero on the OP path");

    auto overSigned = base;
    overSigned.executionPayload.gasLimit = u256(1) << 63;
    auto overSignedError =
        engine_common::op::validateOpNewPayloadRequest(overSigned, /*jovianActive=*/false);
    BOOST_REQUIRE(overSignedError.has_value());
    BOOST_CHECK_EQUAL(*overSignedError, "gasLimit exceeds the maximum block gas limit (2^63-1)");

    auto overUint64 = base;
    overUint64.executionPayload.gasLimit = u256(1) << 64;
    auto overUint64Error =
        engine_common::op::validateOpNewPayloadRequest(overUint64, /*jovianActive=*/false);
    BOOST_REQUIRE(overUint64Error.has_value());
    BOOST_CHECK_EQUAL(
        *overUint64Error, "gasLimit exceeds the uint64 range of the ETH header field");
}

BOOST_AUTO_TEST_CASE(op_fcu_attrs_reject_gas_limit_above_signed_max)
{
    // Matrix: A1 — FCU attrs share newPayload's 2^63-1 cap.
    constexpr auto kMaxBlockGas =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    auto over = holoceneAttributes(bytes(8, 0));
    over.gasLimit = kMaxBlockGas + 1;
    auto overError = engine_common::op::validateOpPayloadAttributes(over, /*jovianActive=*/false);
    BOOST_REQUIRE(overError.has_value());
    BOOST_CHECK_EQUAL(*overError, "gasLimit exceeds the maximum block gas limit (2^63-1)");

    auto atMax = holoceneAttributes(bytes(8, 0));
    atMax.gasLimit = kMaxBlockGas;
    BOOST_CHECK(!engine_common::op::validateOpPayloadAttributes(atMax, /*jovianActive=*/false));
}

BOOST_AUTO_TEST_SUITE_END()
