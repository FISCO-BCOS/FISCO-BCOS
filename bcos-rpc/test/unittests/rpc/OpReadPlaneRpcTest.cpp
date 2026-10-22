/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpReadPlaneRpcTest.cpp
 * @brief EthEndpoint routing over a fake OpCanonicalReader (D1 §10.2): which reads go to the
 *        ledger and which to the facade, on the JSON-RPC surface. The real facade over a real
 *        window is exercised end to end in engine/test/.../OpReadPlaneTest.cpp.
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/engine/OpCanonicalReader.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test
{
namespace
{
/// A window of hand-built blocks: `headChain` maps the unfinalized heights of the head chain to
/// their hash, `window` holds every unfinalized block (both branches) by hash, `headTxs` the
/// transactions the head chain carries. No state views: the fake answers null readers, which
/// the endpoints treat as "no MPT" (not under test here).
struct FakeReader : engine::OpCanonicalReader
{
    BlockRef finalizedRef;
    BlockRef headRef;
    std::optional<protocol::BlockNumber> safe;
    std::map<protocol::BlockNumber, h256> headChain;
    std::map<h256, protocol::Block::Ptr> window;
    std::map<h256, ChainTransaction> headTxs;
    /// A hash the window still LISTS (unfinalizedNumberOf answers) but no longer HOLDS
    /// (unfinalizedBlock null): the finalize race between an endpoint's two lookups.
    std::optional<h256> justFinalized;

    task::Task<BlockRef> finalized() override { co_return finalizedRef; }
    task::Task<BlockRef> head() override { co_return headRef; }
    task::Task<protocol::BlockNumber> safeNumber() override
    {
        co_return safe.value_or(finalizedRef.number);
    }
    task::Task<std::optional<h256>> canonicalHashAt(protocol::BlockNumber number) override
    {
        auto it = headChain.find(number);
        co_return it == headChain.end() ? std::nullopt : std::optional(it->second);
    }
    std::optional<protocol::BlockNumber> unfinalizedNumberOf(h256 const& hash) const override
    {
        if (justFinalized && *justFinalized == hash)
        {
            return finalizedRef.number;
        }
        auto it = window.find(hash);
        return it == window.end() ? std::nullopt :
                                    std::optional(it->second->blockHeader()->number());
    }
    task::Task<protocol::Block::Ptr> unfinalizedBlock(h256 const& hash, int32_t) override
    {
        auto it = window.find(hash);
        co_return it == window.end() ? nullptr : it->second;
    }
    task::Task<std::optional<ChainTransaction>> transactionOnHeadChain(h256 const& txHash) override
    {
        auto it = headTxs.find(txHash);
        co_return it == headTxs.end() ? std::nullopt : std::optional(it->second);
    }
    task::Task<std::shared_ptr<MPTNodeReader>> mptNodeReaderAt(h256 const&) override
    {
        co_return nullptr;
    }
    task::Task<std::shared_ptr<StateStorage>> stateStorageAt(h256 const&) override
    {
        co_return nullptr;
    }
};

struct ReadPlaneTransaction : bcostars::protocol::TransactionImpl
{
    using TransactionImpl::TransactionImpl;
    void markClean() { setTainted(false); }
};

protocol::Transaction::Ptr makeReadPlaneTx(uint64_t nonce, h256 const& hash)
{
    rpc::Web3Transaction web3;
    web3.type = rpc::TransactionType::EIP1559;
    web3.chainId = 1;
    web3.nonce = nonce;
    web3.gasLimit = 21000;
    web3.maxFeePerGas = 150;
    web3.maxPriorityFeePerGas = 20;
    web3.to.emplace(Address("0x1234567890123456789012345678901234567890"));
    auto tars = std::make_shared<bcostars::Transaction>(web3.takeToTarsTransaction());
    tars->extraTransactionHash.assign(hash.begin(), hash.end());
    auto tx = std::make_shared<ReadPlaneTransaction>([tars]() { return tars.get(); });
    Address const sender("0x00000000000000000000000000000000000000b1");
    tx->forceSender(bytes(sender.begin(), sender.end()));
    tx->markClean();
    return tx;
}
}  // namespace

class OpReadPlaneRpcFixture : public RPCFixture
{
public:
    OpReadPlaneRpcFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        m_ledger->setSystemConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
            std::to_string(ledger::OPSTACK_EXECUTOR_VERSION));
        nodeService->setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);

        // The ledger holds 0..tip (finalized); the window holds two siblings at tip+1, the
        // head chain runs through sibling B.
        tip = m_ledger->blockNumber();
        tipHash = protocol::canonicalBlockHash(*m_ledger->ledgerData().at(tip)->blockHeader());
        reader = std::make_shared<FakeReader>();
        reader->finalizedRef = {tipHash, tip};
        a = windowBlock(0xa1);
        b = windowBlock(0xb1);
        hashA = a.hash;
        hashB = b.hash;
        txA = a.txHash;
        txB = b.txHash;
        BOOST_REQUIRE_NE(hashA.hex(), hashB.hex());
        reader->window[hashA] = a.block;
        reader->window[hashB] = b.block;
        setHead(b);
        nodeService->setOpCanonicalReader(reader);
    }

    /// A window block with the transaction and receipt objects the facade hands out.
    struct WindowBlock
    {
        protocol::Block::Ptr block;
        protocol::Transaction::Ptr tx;
        protocol::TransactionReceipt::Ptr receipt;
        h256 hash;
        h256 txHash;
    };

    void setHead(WindowBlock const& head)
    {
        reader->headRef = {head.hash, tip + 1};
        reader->headChain = {{tip + 1, head.hash}};
        reader->headTxs.clear();
        reader->headTxs[head.txHash] = engine::OpCanonicalReader::ChainTransaction{
            .transaction = head.tx, .receipt = head.receipt, .blockHash = head.hash};
    }

    /// One block at tip+1 with one transaction whose receipt carries one log; @p salt makes
    /// the header hash and the tx hash distinct per sibling.
    WindowBlock windowBlock(uint8_t salt)
    {
        auto block = m_blockFactory->createBlock();
        auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(tip + 1);
        // Whole seconds: the OP (Eth-shaped) header hash rejects a sub-second timestamp.
        header->setTimestamp(1'700'000'000'000ULL + 1000ULL * salt);
        header->setParentInfo(protocol::ParentInfo{.blockNumber = tip, .blockHash = tipHash});
        header->setGasLimit(30'000'000);
        header->setGasUsed(21'000);
        // OP shape (isOpEthereumBlock): Holocene extraData 250/6, base fee 100, withdrawals
        // root; the salt keeps the sibling hashes apart through the timestamp.
        header->setExtraData(bytes{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06});
        header->setBaseFee(100);
        header->setWithdrawalsRoot(h256(1U));
        header->setExcessBlobGas(0);
        header->setBlobGasUsed(0);
        header->calculateHash(*cryptoSuite->hashImpl());
        block->setBlockHeader(header);
        h256 const txHash(static_cast<unsigned>(salt));
        auto tx = makeReadPlaneTx(0, txHash);
        std::vector<protocol::LogEntry> logs;
        // Four topics: the filter's empty `topics` array parses into positional sets, and a
        // log with fewer topics than positions never matches (LogMatcher::matches).
        logs.emplace_back(bytes(20, salt), h256s{h256(1U), h256(2U), h256(3U), h256(4U)}, bytes{});
        auto receipt =
            m_blockFactory->receiptFactory()->createReceipt(21'000, "", logs, 0, {}, tip + 1);
        receipt->setTransactionIndex(0);
        block->appendTransaction(tx);
        block->appendReceipt(receipt);
        block->appendTransactionMetaData(
            m_blockFactory->createTransactionMetaData(txHash, std::string{}));
        return WindowBlock{.block = block,
            .tx = tx,
            .receipt = receipt,
            .hash = protocol::canonicalBlockHash(*header),
            .txHash = txHash};
    }

    Json::Value call(std::string const& method, Json::Value params = Json::arrayValue)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = method;
        req["params"] = std::move(params);
        std::promise<bytes> promise;
        web3JsonRpc->onRPCRequest(
            printJson(req), [&promise](bytes resp, boost::beast::http::status) {
                promise.set_value(std::move(resp));
            });
        auto const raw = promise.get_future().get();
        Json::Value value;
        Json::Reader().parse(std::string(raw.begin(), raw.end()), value);
        return value;
    }
    static Json::Value params(std::initializer_list<Json::Value> values)
    {
        Json::Value out(Json::arrayValue);
        for (auto const& v : values)
        {
            out.append(v);
        }
        return out;
    }
    Json::Value blockByNumber(std::string const& tag)
    {
        return call("eth_getBlockByNumber", params({tag, false}))["result"];
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
    std::shared_ptr<FakeReader> reader;
    protocol::BlockNumber tip = 0;
    h256 tipHash;
    WindowBlock a;
    WindowBlock b;
    h256 hashA;
    h256 hashB;
    h256 txA;
    h256 txB;
};

BOOST_FIXTURE_TEST_SUITE(OpReadPlaneRpcTest, OpReadPlaneRpcFixture)

// latest = the facade's head, finalized = its finalized tip, safe = tracker or finalized; the
// [web3_rpc] depths do not apply on this lane.
BOOST_AUTO_TEST_CASE(TagsFollowTheFacade)
{
    nodeService->setSafeBlockDepth(3);
    nodeService->setFinalizedBlockDepth(7);
    auto const headTag = toQuantity(tip + 1);
    auto const tipTag = toQuantity(tip);
    BOOST_CHECK_EQUAL(call("eth_blockNumber")["result"].asString(), headTag);
    BOOST_CHECK_EQUAL(blockByNumber("latest")["hash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK_EQUAL(blockByNumber(headTag)["hash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK_EQUAL(blockByNumber("finalized")["number"].asString(), tipTag);
    BOOST_CHECK_EQUAL(blockByNumber("finalized")["hash"].asString(), tipHash.hexPrefixed());
    BOOST_CHECK_EQUAL(blockByNumber("safe")["number"].asString(), tipTag);
    reader->safe = tip + 1;
    BOOST_CHECK_EQUAL(blockByNumber("safe")["hash"].asString(), hashB.hexPrefixed());
    // Finalized heights read the ledger exactly as before.
    BOOST_CHECK_EQUAL(blockByNumber(tipTag)["hash"].asString(), tipHash.hexPrefixed());
    BOOST_CHECK_EQUAL(blockByNumber("0x1")["number"].asString(), "0x1");
    // Above the head: null.
    BOOST_CHECK(blockByNumber(toQuantity(tip + 2)).isNull());
}

// By hash: any window block, from its own rows. By number: the head chain only.
BOOST_AUTO_TEST_CASE(SideBranchByHashOnly)
{
    auto const headTag = toQuantity(tip + 1);
    auto sideBlock = call("eth_getBlockByHash", params({hashA.hexPrefixed(), true}))["result"];
    BOOST_REQUIRE(sideBlock.isObject());
    BOOST_CHECK_EQUAL(sideBlock["number"].asString(), headTag);
    BOOST_CHECK_EQUAL(sideBlock["hash"].asString(), hashA.hexPrefixed());
    BOOST_CHECK_EQUAL(sideBlock["transactions"].size(), 1U);
    BOOST_CHECK_EQUAL(
        call("eth_getBlockTransactionCountByHash", params({hashA.hexPrefixed()}))["result"]
            .asString(),
        "0x1");
    auto sideReceipts = call("eth_getBlockReceipts", params({hashA.hexPrefixed()}))["result"];
    BOOST_REQUIRE(sideReceipts.isArray());
    BOOST_REQUIRE_EQUAL(sideReceipts.size(), 1U);
    BOOST_CHECK_EQUAL(sideReceipts[0U]["blockHash"].asString(), hashA.hexPrefixed());
    auto byIndex = call(
        "eth_getTransactionByBlockHashAndIndex", params({hashA.hexPrefixed(), "0x0"}))["result"];
    BOOST_REQUIRE(byIndex.isObject());
    BOOST_CHECK_EQUAL(byIndex["blockHash"].asString(), hashA.hexPrefixed());
    // Never by number.
    BOOST_CHECK_EQUAL(blockByNumber(headTag)["hash"].asString(), hashB.hexPrefixed());
    auto headReceipts = call("eth_getBlockReceipts", params({headTag}))["result"];
    BOOST_REQUIRE_EQUAL(headReceipts.size(), 1U);
    BOOST_CHECK_EQUAL(headReceipts[0U]["blockHash"].asString(), hashB.hexPrefixed());
    // Switch the head to A: the same reads now describe A.
    setHead(a);
    BOOST_CHECK_EQUAL(blockByNumber("latest")["hash"].asString(), hashA.hexPrefixed());
    BOOST_CHECK_EQUAL(
        call("eth_getBlockReceipts", params({headTag}))["result"][0U]["blockHash"].asString(),
        hashA.hexPrefixed());
}

// Transactions: the ledger first (finalized), then the head chain's window; a side-branch-only
// transaction is null, as geth answers for a non-canonical inclusion.
BOOST_AUTO_TEST_CASE(TransactionsLedgerThenHeadChain)
{
    auto const headTag = toQuantity(tip + 1);
    auto receiptB = call("eth_getTransactionReceipt", params({txB.hexPrefixed()}))["result"];
    BOOST_REQUIRE(receiptB.isObject());
    BOOST_CHECK_EQUAL(receiptB["blockHash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK_EQUAL(receiptB["blockNumber"].asString(), headTag);
    auto txJson = call("eth_getTransactionByHash", params({txB.hexPrefixed()}))["result"];
    BOOST_REQUIRE(txJson.isObject());
    BOOST_CHECK_EQUAL(txJson["blockHash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK(call("eth_getTransactionReceipt", params({txA.hexPrefixed()}))["result"].isNull());
    BOOST_CHECK(call("eth_getTransactionByHash", params({txA.hexPrefixed()}))["result"].isNull());
    // A finalized transaction is the ledger's, whatever the head (FakeLedger serves the
    // receipts it was handed through storeTxAndReceipt).
    h256 const ledgerTx(0xeeU);
    m_ledger->storeTxAndReceipt(
        makeReadPlaneTx(7, ledgerTx), m_blockFactory->receiptFactory()->createReceipt(21'000, "",
                                          std::vector<protocol::LogEntry>{}, 0, {}, tip));
    auto ledgerReceipt =
        call("eth_getTransactionReceipt", params({ledgerTx.hexPrefixed()}))["result"];
    BOOST_REQUIRE(ledgerReceipt.isObject());
    BOOST_CHECK_EQUAL(ledgerReceipt["blockHash"].asString(), tipHash.hexPrefixed());
    setHead(a);
    BOOST_CHECK(call("eth_getTransactionReceipt", params({txB.hexPrefixed()}))["result"].isNull());
    BOOST_CHECK_EQUAL(
        call("eth_getTransactionReceipt", params({txA.hexPrefixed()}))["result"]["blockHash"]
            .asString(),
        hashA.hexPrefixed());
}

// eth_getLogs over an unfinalized height scans the head chain's block; a range that ends at
// `latest` reaches it.
BOOST_AUTO_TEST_CASE(LogsOverTheWindow)
{
    auto const headTag = toQuantity(tip + 1);
    Json::Value filter(Json::objectValue);
    filter["fromBlock"] = headTag;
    filter["toBlock"] = "latest";
    filter["address"] = Json::Value(Json::arrayValue);
    filter["topics"] = Json::Value(Json::arrayValue);
    auto logs = call("eth_getLogs", params({filter}))["result"];
    BOOST_REQUIRE(logs.isArray());
    BOOST_REQUIRE_EQUAL(logs.size(), 1U);
    BOOST_CHECK_EQUAL(logs[0U]["blockHash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK_EQUAL(logs[0U]["blockNumber"].asString(), headTag);
    setHead(a);
    logs = call("eth_getLogs", params({filter}))["result"];
    BOOST_REQUIRE_EQUAL(logs.size(), 1U);
    BOOST_CHECK_EQUAL(logs[0U]["blockHash"].asString(), hashA.hexPrefixed());
    // By block hash: the side branch's own logs.
    Json::Value byHash(Json::objectValue);
    // FilterRequest::fromJson reads the lowercase "blockhash" key and insists on the range
    // keys even then (pre-existing parser behaviour, not this change's).
    byHash["blockhash"] = hashB.hexPrefixed();
    byHash["fromBlock"] = "latest";
    byHash["toBlock"] = "latest";
    byHash["address"] = Json::Value(Json::arrayValue);
    byHash["topics"] = Json::Value(Json::arrayValue);
    logs = call("eth_getLogs", params({byHash}))["result"];
    BOOST_REQUIRE_EQUAL(logs.size(), 1U);
    BOOST_CHECK_EQUAL(logs[0U]["blockHash"].asString(), hashB.hexPrefixed());
}

// The remaining rerouted reads, each against the head chain's window block: by-number index
// lookups, block tx count, the head base fee behind eth_gasPrice, eth_feeHistory's block
// source, and block filters.
BOOST_AUTO_TEST_CASE(RemainingReadsFollowTheHeadChain)
{
    auto const headTag = toQuantity(tip + 1);
    auto byIndex =
        call("eth_getTransactionByBlockNumberAndIndex", params({headTag, "0x0"}))["result"];
    BOOST_REQUIRE(byIndex.isObject());
    BOOST_CHECK_EQUAL(byIndex["blockHash"].asString(), hashB.hexPrefixed());
    BOOST_CHECK_EQUAL(byIndex["hash"].asString(), txB.hexPrefixed());
    // Out of range on the window path is the same InvalidParams as on the ledger path.
    auto outOfRange = call("eth_getTransactionByBlockNumberAndIndex", params({headTag, "0x1"}));
    BOOST_CHECK(outOfRange["result"].isNull());
    BOOST_CHECK_EQUAL(
        call("eth_getBlockTransactionCountByNumber", params({headTag}))["result"].asString(),
        "0x1");
    // eth_gasPrice = head base fee (the window block's 100) + the OP suggested tip.
    auto const expectedGasPrice = toQuantity(
        u256(100) + u256(rpc::suggestedPriorityFeeWei(ledger::OPSTACK_EXECUTOR_VERSION)));
    BOOST_CHECK_EQUAL(call("eth_gasPrice")["result"].asString(), expectedGasPrice);
    // eth_feeHistory with newest = latest reads the window block through the block source.
    auto fees =
        call("eth_feeHistory", params({"0x1", "latest", Json::Value(Json::arrayValue)}))["result"];
    BOOST_REQUIRE(fees.isObject());
    BOOST_CHECK_EQUAL(fees["oldestBlock"].asString(), headTag);
    BOOST_REQUIRE_EQUAL(fees["baseFeePerGas"].size(), 2U);
    BOOST_CHECK_EQUAL(fees["baseFeePerGas"][0U].asString(), "0x64");
    // Switch the head: the same reads describe A.
    setHead(a);
    BOOST_CHECK_EQUAL(call("eth_getTransactionByBlockNumberAndIndex",
                          params({headTag, "0x0"}))["result"]["blockHash"]
                          .asString(),
        hashA.hexPrefixed());
}

// engine_getPayloadBodiesByRange/ByHash resolve through the same canonical-chain helpers: an
// unfinalized head block answers its body, a window block by hash too. A plain (pre-Shanghai
// shaped, transaction-less) block is used because a Canyon+ OP block's body is null on this
// lane regardless (SYS_NUMBER_2_WITHDRAWALS is never written by the OP lane).
BOOST_AUTO_TEST_CASE(PayloadBodiesFollowTheHeadChain)
{
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(tip + 1);
    header->setTimestamp(1'700'000'000'000ULL + 1000ULL * 0xc1);
    header->setParentInfo(protocol::ParentInfo{.blockNumber = tip, .blockHash = tipHash});
    header->setGasLimit(30'000'000);
    header->calculateHash(*cryptoSuite->hashImpl());
    block->setBlockHeader(header);
    WindowBlock const c{.block = block,
        .tx = nullptr,
        .receipt = nullptr,
        .hash = protocol::canonicalBlockHash(*header),
        .txHash = {}};
    reader->window[c.hash] = block;
    reader->headRef = {c.hash, tip + 1};
    reader->headChain = {{tip + 1, c.hash}};
    reader->headTxs.clear();

    rpc::EngineEndpoint engine(nodeService);
    auto invoke = [&](auto method, Json::Value const& request) {
        Json::Value response;
        task::syncWait((engine.*method)(request, response));
        return response["result"];
    };
    auto const headTag = toQuantity(tip + 1);
    auto byRange =
        invoke(&rpc::EngineEndpoint::getPayloadBodiesByRangeV1, params({headTag, "0x1"}));
    BOOST_REQUIRE(byRange.isArray());
    BOOST_REQUIRE_EQUAL(byRange.size(), 1U);
    BOOST_REQUIRE(byRange[0U].isObject());
    BOOST_CHECK_EQUAL(byRange[0U]["transactions"].size(), 0U);
    auto byHash = invoke(&rpc::EngineEndpoint::getPayloadBodiesByHashV1,
        params({params({c.hash.hexPrefixed(), h256(0x77U).hexPrefixed()})}));
    BOOST_REQUIRE_EQUAL(byHash.size(), 2U);
    BOOST_CHECK(byHash[0U].isObject());
    BOOST_CHECK(byHash[1U].isNull());
    // Above the head: an empty array, as before.
    BOOST_CHECK(invoke(
        &rpc::EngineEndpoint::getPayloadBodiesByRangeV1, params({toQuantity(tip + 2), "0x1"}))
            .empty());
}

// eth_newBlockFilter + eth_getFilterChanges: a block that becomes the head after the filter
// was installed is reported with the canonical hash at its height.
BOOST_AUTO_TEST_CASE(BlockFilterSeesTheNewHead)
{
    // No head above the finalized tip yet: latest == finalized.
    reader->headRef = reader->finalizedRef;
    reader->headChain.clear();
    auto const filterId = call("eth_newBlockFilter")["result"].asString();
    BOOST_CHECK(call("eth_getFilterChanges", params({filterId}))["result"].empty());
    setHead(b);
    auto changes = call("eth_getFilterChanges", params({filterId}))["result"];
    BOOST_REQUIRE(changes.isArray());
    BOOST_REQUIRE_EQUAL(changes.size(), 1U);
    BOOST_CHECK_EQUAL(changes[0U].asString(), hashB.hexPrefixed());
}

// The finalize race on eth_getProof by hash: the window still lists the hash when the
// endpoint resolves it but has handed the block to the ledger by the time the state read
// runs. The read must fall through to the ledger + committed plane (here: no committed MPT
// reader is wired, so -32603 "MPT not enabled"), never answer "Block not found".
BOOST_AUTO_TEST_CASE(GetProofByHashSurvivesFinalizeRace)
{
    reader->justFinalized = tipHash;
    auto resp = call("eth_getProof", params({"0x00000000000000000000000000000000000000b1",
                                         Json::Value(Json::arrayValue), tipHash.hexPrefixed()}));
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_NE(resp["error"]["message"].asString(), "Block not found");
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "MPT not enabled on this node");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
