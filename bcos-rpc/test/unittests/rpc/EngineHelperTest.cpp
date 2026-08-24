/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// op-geth golden corpus 真实 raw tx（val-loop bcos-evm/test/opstack/t8n/golden/engine/
// isthmus_access_list.golden.json rawTransactions[0]/[1]，逐字节核验一致）：0x7E deposit +
// 0x02 EIP-1559 typed。两者经 decodeTransaction 必抛 TarsDecodeMismatch（tars 误解析 RLP）——
// transactions 填充分支容错跳过，rawTransactions 是 W1 的核心断言对象。
constexpr std::string_view kDepositRawTx =
    "0x7ef90104a0ae1a1f61e85683e8084e5004f4c36b050cb2ea1e5f78f1e7d518163ade648a7994deaddeaddeaddead"
    "deaddeaddeaddeaddead00019442000000000000000000000000000000000000158080830f424080b8b0098999be00"
    "000558000c5fc500000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000006fc23ac0000000000000000000000000000000000000000000000000000000000000f42"
    "4000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000";
constexpr std::string_view kEip1559RawTx =
    "0x02f8e0822105808405f5e1008477359400830186a094c0de0000000000000000000000000000000000058080f872"
    "f85994c0de000000000000000000000000000000000005f842a0000000000000000000000000000000000000000000"
    "0000000000000000000000a00000000000000000000000000000000000000000000000000000000000000005d694dd"
    "ddddddddddddddddddddddddddddddddddddddc080a0394ee65265d6d1eead7675c85dc4dfa781d7d9b1d27392f51d"
    "d7feb5d8f2f27ba0434c6ec29d5d0e3b781f461538d61e7120c5f25a33dd5f8a230aa8259dba7d0c";
constexpr std::string_view kWithdrawalsRoot =
    "0x1111111111111111111111111111111111111111111111111111111111111111";

std::shared_ptr<bcos::protocol::TransactionFactory> makeTxFactory()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
}

Json::Value makePayloadParams(
    std::vector<std::string_view> rawTxs, std::string_view withdrawalsRoot)
{
    Json::Value ep(Json::objectValue);
    ep["parentHash"] = "0x" + std::string(64, '0');
    ep["stateRoot"] = "0x" + std::string(64, '0');
    ep["receiptsRoot"] = "0x" + std::string(64, '0');
    ep["prevRandao"] = "0x" + std::string(64, '0');
    ep["gasLimit"] = "0x1c9c380";
    ep["gasUsed"] = "0x0";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0x" + std::string(64, '0');
    ep["feeRecipient"] = "0x" + std::string(40, '0');
    ep["timestamp"] = "0x1";
    ep["blockNumber"] = "0x1";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["extraData"] = "0x";
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    ep["blobGasUsed"] = "0x0";  // V4 field-shape requirement (Isthmus+ fields)
    ep["excessBlobGas"] = "0x0";
    if (withdrawalsRoot.size())
    {
        ep["withdrawalsRoot"] = std::string(withdrawalsRoot);
    }
    Json::Value txs(Json::arrayValue);
    for (auto const& raw : rawTxs)
    {
        txs.append(std::string(raw));
    }
    ep["transactions"] = txs;
    Json::Value params(Json::arrayValue);
    params.append(ep);
    // V4 param shape: [executionPayload, expectedBlobVersionedHashes, parentBeaconBlockRoot,
    // executionRequests] — requireNewPayloadV4ParamShape rejects a 1-element params array.
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes: none
    params.append("0x" + std::string(64, '0'));    // parentBeaconBlockRoot
    params.append(Json::Value(Json::arrayValue));  // executionRequests: empty
    return params;
}

BOOST_AUTO_TEST_SUITE(EngineHelperTest)

// rawTransactions 逐字节保留输入；transactions 同步解码。
BOOST_AUTO_TEST_CASE(parseFillsRawTransactionsPreservingBytes)
{
    auto params = makePayloadParams({kDepositRawTx, kEip1559RawTx}, kWithdrawalsRoot);
    auto factory = makeTxFactory();
    // 不得抛：transactions 填充分支容错跳过 decode 失败的 typed tx。
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.rawTransactions.has_value());
    BOOST_REQUIRE_EQUAL(request.executionPayload.rawTransactions->size(), 2u);
    // 逐字节比对：rawTransactions[i] == fromHex(rawTx[i])
    for (int i = 0; i < 2; ++i)
    {
        auto expect =
            fromHex(kDepositRawTx == params[0]["transactions"][i].asString() ? kDepositRawTx :
                                                                               kEip1559RawTx);
        BOOST_CHECK(request.executionPayload.rawTransactions->at(i) == expect);
    }
    // 合并后语义（上游 parse 不解码，解码交给执行时 dispatch table）：transactions 以
    // EngineTransaction{raw, decoded=nullptr} 携带 wire 字节；rawTransactions 保留 OP 载体。
    BOOST_REQUIRE_EQUAL(request.executionPayload.transactions.size(), 2u);
    for (int i = 0; i < 2; ++i)
    {
        auto expect =
            fromHex(kDepositRawTx == params[0]["transactions"][i].asString() ? kDepositRawTx :
                                                                               kEip1559RawTx);
        BOOST_CHECK(request.executionPayload.transactions[i].raw == expect);
        BOOST_CHECK(request.executionPayload.transactions[i].decoded == nullptr);
    }
}

// withdrawalsRoot 正确解析为 h256。
BOOST_AUTO_TEST_CASE(parseFillsWithdrawalsRoot)
{
    auto params = makePayloadParams({kEip1559RawTx}, kWithdrawalsRoot);
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.withdrawalsRoot.has_value());
    auto expect = fromHex(kWithdrawalsRoot);
    BOOST_CHECK_EQUAL(request.executionPayload.withdrawalsRoot->size(), 32u);
    BOOST_CHECK(std::equal(
        expect.begin(), expect.end(), request.executionPayload.withdrawalsRoot->begin()));
}

// withdrawalsRoot 缺省 → V4 形状校验拒绝（Isthmus 必填字段）；错长（31/33 字节）→
// bcos::rpc::JsonRpcException。
BOOST_AUTO_TEST_CASE(parseWithdrawalsRootBoundaries)
{
    // V4 shape validation requires withdrawalsRoot as a hex string (Isthmus contract) — a
    // missing key is a shape rejection, not a silent nullopt.
    auto noRoot = makePayloadParams({kEip1559RawTx}, "");
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(noRoot, engine::ApiVersion::V4), bcos::rpc::JsonRpcException);

    // 31 字节（62 hex）
    auto badShort = makePayloadParams({kEip1559RawTx}, "0x" + std::string(62, '1'));
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(badShort, engine::ApiVersion::V4), bcos::rpc::JsonRpcException);
    // 33 字节（66 hex）
    auto badLong = makePayloadParams({kEip1559RawTx}, "0x" + std::string(66, '1'));
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(badLong, engine::ApiVersion::V4), bcos::rpc::JsonRpcException);
}

// 缺 transactions 键 → rawTransactions 保持 nullopt（OP 校验 :299 仍拒）。
BOOST_AUTO_TEST_CASE(parseMissingTransactionsLeavesRawNullopt)
{
    // withdrawalsRoot 必须给合法值：V4 形状校验（Isthmus 必填）会先于本测试关注的
    // transactions 缺省语义拒绝缺根载荷。
    auto params = makePayloadParams({}, kWithdrawalsRoot);
    params[0].removeMember("transactions");
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_CHECK(!request.executionPayload.rawTransactions.has_value());
}

// transactions 存在但为空数组 → rawTransactions present-and-empty（OP 校验 :299 接受）。
BOOST_AUTO_TEST_CASE(parseEmptyTransactionsIsPresentAndEmpty)
{
    auto params = makePayloadParams({}, kWithdrawalsRoot);
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.rawTransactions.has_value());
    BOOST_CHECK(request.executionPayload.rawTransactions->empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
