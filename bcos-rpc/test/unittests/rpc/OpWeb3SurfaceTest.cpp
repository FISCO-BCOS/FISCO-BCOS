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
 * @file OpWeb3SurfaceTest.cpp
 * @brief The OP-lane Web3 surface op-batcher / op-proposer / op-node call: eth_feeHistory,
 *        eth_getBlockReceipts, engine-aware eth_syncing, txpool_status / txpool_content.
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/engine/AnyEngineService.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/FeeHistory.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string>
#include <vector>
#ifdef FISCO_WITH_CXX_MODULES
import bcos.utilities;
#else
#include <bcos-utilities/DataConvertUtility.h>
#endif

namespace bcos::test
{
namespace
{
/// Admission clears the taint flag; the mempool refuses a tainted tx.
struct CleanTransaction : bcostars::protocol::TransactionImpl
{
    using TransactionImpl::TransactionImpl;
    void markClean() { setTainted(false); }
};

/// A web3 tx with a fixed hash and sender (the tars form the ledger and mempool hold).
protocol::Transaction::Ptr makeTx(rpc::Web3Transaction web3, h256 const& hash, Address const& from)
{
    auto tars = std::make_shared<bcostars::Transaction>(web3.takeToTarsTransaction());
    tars->extraTransactionHash.assign(hash.begin(), hash.end());
    auto tx = std::make_shared<CleanTransaction>([tars]() { return tars.get(); });
    tx->forceSender(bytes(from.begin(), from.end()));
    tx->markClean();
    return tx;
}

rpc::Web3Transaction eip1559(uint64_t nonce, u256 maxFee, u256 tip)
{
    rpc::Web3Transaction web3;
    web3.type = rpc::TransactionType::EIP1559;
    web3.chainId = 1;
    web3.nonce = nonce;
    web3.gasLimit = 21000;
    web3.maxFeePerGas = maxFee;
    web3.maxPriorityFeePerGas = tip;
    web3.to.emplace(Address("0x1234567890123456789012345678901234567890"));
    return web3;
}

rpc::Web3Transaction deposit()
{
    rpc::Web3Transaction web3;
    web3.type = rpc::TransactionType::Deposit;
    web3.from = Address("0xdead000000000000000000000000000000000001");
    web3.sourceHash = h256(7U);
    web3.gasLimit = 1'000'000;
    web3.isSystemTx = false;
    return web3;
}

/// An engine whose last applied forkchoice head the test sets.
struct HeadEngine
{
    std::shared_ptr<std::optional<protocol::BlockNumber>> head =
        std::make_shared<std::optional<protocol::BlockNumber>>();

    task::Task<std::vector<std::string>> exchangeCapabilities(std::vector<std::string> caps)
    {
        co_return caps;
    }
    task::Task<engine::ForkchoiceUpdatedResult> updateForkchoice(
        const engine::ForkchoiceState&, const engine::PayloadAttributes*, std::uint32_t)
    {
        co_return engine::ForkchoiceUpdatedResult{};
    }
    task::Task<engine::GetPayloadResult> getPayload(const engine::PayloadID&, std::uint32_t)
    {
        co_return std::make_unique<engine::GetPayloadData>();
    }
    task::Task<engine::PayloadStatus> newPayload(const engine::NewPayloadRequest&, std::uint32_t)
    {
        co_return engine::PayloadStatus{};
    }
    std::optional<protocol::BlockNumber> getSafeBlockNumber() const { return std::nullopt; }
    std::optional<protocol::BlockNumber> getFinalizedBlockNumber() const { return std::nullopt; }
    std::optional<protocol::BlockNumber> getHeadBlockNumber() const { return *head; }
};
}  // namespace

class OpWeb3SurfaceFixture : public RPCFixture
{
public:
    using StateStorage = storage2::memory_storage::MemoryStorage<executor_v1::StateKey,
        executor_v1::StateValue, storage2::memory_storage::ORDERED>;

    OpWeb3SurfaceFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
    }

    void setExecutorVersion(int version)
    {
        m_ledger->setSystemConfig(
            magic_enum::enum_name(ledger::SystemConfig::executor_version), std::to_string(version));
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

    protocol::BlockHeader::Ptr header(protocol::BlockNumber number)
    {
        return m_ledger->ledgerData().at(number)->blockHeader();
    }

    /// OP-shape the header at @p number: Holocene extraData (denominator 250, elasticity 6).
    void opHeader(protocol::BlockNumber number, u256 baseFee, u256 gasUsed)
    {
        auto h = header(number);
        h->setBaseFee(baseFee);
        h->setGasLimit(30'000'000);
        h->setGasUsed(gasUsed);
        h->setExtraData(bytes{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06});
        h->setExcessBlobGas(0);
        h->setBlobGasUsed(0);
        h->setWithdrawalsRoot(h256(1U));  // with baseFee: isOpEthereumBlock
        h->calculateHash(*cryptoSuite->hashImpl());
    }

    /// Replace the latest block (19) with an OP block holding a deposit then a 1559 tx, both
    /// stored with receipts. Returns the two tx hashes.
    std::vector<h256> blockWithDepositAndTx()
    {
        auto const number = m_ledger->blockNumber();
        opHeader(number, 100, 63'000);
        auto block = m_blockFactory->createBlock();
        block->setBlockHeader(header(number));
        std::vector<h256> hashes;
        std::vector<u256> const gasUsed{42'000, 21'000};
        for (std::size_t i = 0; i < gasUsed.size(); ++i)
        {
            h256 const hash(0xa0U + i);
            auto tx = makeTx(i == 0 ? deposit() : eip1559(0, 150, 20), hash,
                Address("0x00000000000000000000000000000000000000b1"));
            std::vector<protocol::LogEntry> logs;
            auto receipt = m_blockFactory->receiptFactory()->createReceipt(
                gasUsed[i], "", logs, 0, bytesConstRef{}, number);
            receipt->setTransactionIndex(i);
            if (tx->isDepositTx())
            {
                protocol::OpStackReceiptMeta meta;
                meta.deposit_nonce = 9;
                meta.deposit_receipt_version = 1;
                receipt->setOpStackMeta(meta);
            }
            block->appendTransaction(tx);
            block->appendReceipt(receipt);
            block->appendTransactionMetaData(
                m_blockFactory->createTransactionMetaData(hash, std::string{}));
            m_ledger->storeTxAndReceipt(tx, receipt);
            hashes.push_back(hash);
        }
        m_ledger->replaceBlock(block);
        return hashes;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(OpWeb3SurfaceTest, OpWeb3SurfaceFixture)

// geth EffectiveGasTip: min(tip, maxFee - baseFee), floored at 0; a legacy tx's gas price sits in
// both fee fields.
BOOST_AUTO_TEST_CASE(EffectivePriorityFee)
{
    auto const tx = makeTx(eip1559(0, 100, 10), h256(1U), Address());
    BOOST_CHECK_EQUAL(rpc::effectivePriorityFee(*tx, 95), 5);
    BOOST_CHECK_EQUAL(rpc::effectivePriorityFee(*tx, 50), 10);
    BOOST_CHECK_EQUAL(rpc::effectivePriorityFee(*tx, 120), 0);
    auto legacyWeb3 = eip1559(0, 100, 100);
    legacyWeb3.type = rpc::TransactionType::Legacy;
    BOOST_CHECK_EQUAL(
        rpc::effectivePriorityFee(*makeTx(std::move(legacyWeb3), h256(2U), Address()), 60), 40);
}

// Hand-computed: sorted (1, 21000) (3, 42000) (5, 21000); cumulative 21000 / 63000 / 84000.
BOOST_AUTO_TEST_CASE(RewardPercentilesGasWeighted)
{
    std::vector<rpc::RewardSample> const samples{{5, 21000}, {1, 21000}, {3, 42000}};
    std::vector<double> const percentiles{0, 25, 26, 50, 75, 100};
    auto const row = rpc::rewardPercentiles(samples, percentiles, 84000);
    // thresholds 0, 21000, 21840, 42000, 63000, 84000
    std::vector<u256> const expected{1, 1, 3, 3, 3, 5};
    BOOST_CHECK(row == expected);
    BOOST_CHECK(rpc::rewardPercentiles({}, percentiles, 84000) == std::vector<u256>(6, 0));
    BOOST_CHECK(rpc::rewardPercentiles(samples, percentiles, 0) == std::vector<u256>(6, 0));
}

// op-geth CalcBaseFee, hand-computed with parent gasLimit 30M and baseFee 1 gwei.
BOOST_AUTO_TEST_CASE(NextOpBaseFeeByParentShape)
{
    auto parent = [&](bytes extra, u256 gasUsed, bool withdrawals) {
        auto h = m_blockFactory->blockHeaderFactory()->createBlockHeader();
        h->setGasLimit(30'000'000);
        h->setGasUsed(gasUsed);
        h->setBaseFee(1'000'000'000);
        h->setBlobGasUsed(0);
        h->setExtraData(std::move(extra));
        if (withdrawals)
        {
            h->setWithdrawalsRoot(h256(1U));
        }
        return h;
    };
    bytes const holocene{0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};  // 250 / 6
    // target 5M, used 10M: + 1e9 * 5M / 5M / 250 = +4,000,000
    BOOST_CHECK_EQUAL(rpc::nextOpBaseFee(*parent(holocene, 10'000'000, true)), 1'004'000'000);
    // Holocene params come from extraData: 8 / 2, target 15M, used 30M: + 1e9 / 8
    bytes const eightTwo{0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02};
    BOOST_CHECK_EQUAL(rpc::nextOpBaseFee(*parent(eightTwo, 30'000'000, true)), 1'125'000'000);
    // Jovian (17 bytes, 0x01): empty block lowers the fee to 996e6, the 2 gwei floor wins.
    bytes jovian{0x01, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
    auto const floor = bytes{0x00, 0x00, 0x00, 0x00, 0x77, 0x35, 0x94, 0x00};  // 2e9
    jovian.insert(jovian.end(), floor.begin(), floor.end());
    BOOST_CHECK_EQUAL(rpc::nextOpBaseFee(*parent(jovian, 0, true)), 2'000'000'000);
    // Pre-Holocene, Canyon child (parent has withdrawalsRoot): 250 / 6, same as the first case.
    BOOST_CHECK_EQUAL(rpc::nextOpBaseFee(*parent({}, 10'000'000, true)), 1'004'000'000);
    // Pre-Canyon: denominator 50: + 1e9 * 5M / 5M / 50 = +20,000,000
    BOOST_CHECK_EQUAL(rpc::nextOpBaseFee(*parent({}, 10'000'000, false)), 1'020'000'000);
}

BOOST_AUTO_TEST_CASE(FeeHistoryShape)
{
    opHeader(17, 1000, 15'000'000);
    opHeader(18, 1100, 5'000'000);
    opHeader(19, 1200, 10'000'000);
    auto const resp = call("eth_feeHistory", params({"0x3", "latest", Json::arrayValue}));
    BOOST_REQUIRE_MESSAGE(resp.isMember("result"), printJson(resp));
    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["oldestBlock"].asString(), "0x11");
    auto const& fees = result["baseFeePerGas"];
    BOOST_REQUIRE_EQUAL(fees.size(), 4U);
    BOOST_CHECK_EQUAL(fees[0].asString(), "0x3e8");
    BOOST_CHECK_EQUAL(fees[2].asString(), "0x4b0");
    // 1200 + 1200 * 5M / 5M / 250 = 1204
    BOOST_CHECK_EQUAL(fees[3].asString(), toQuantity(rpc::nextOpBaseFee(*header(19))));
    BOOST_CHECK_EQUAL(fees[3].asString(), "0x4b4");
    BOOST_REQUIRE_EQUAL(result["gasUsedRatio"].size(), 3U);
    BOOST_CHECK_CLOSE(result["gasUsedRatio"][0].asDouble(), 0.5, 1e-9);
    BOOST_CHECK(!result.isMember("reward"));
    // Blob series: 0x1 (minBlobGasPrice) for headers with excessBlobGas, ratio 0.
    BOOST_REQUIRE_EQUAL(result["baseFeePerBlobGas"].size(), 4U);
    BOOST_REQUIRE_EQUAL(result["blobGasUsedRatio"].size(), 3U);
    BOOST_CHECK_EQUAL(result["baseFeePerBlobGas"][0].asString(), "0x1");
    BOOST_CHECK_EQUAL(result["baseFeePerBlobGas"][3].asString(), "0x1");
    BOOST_CHECK_EQUAL(result["blobGasUsedRatio"][2].asDouble(), 0.0);
    // A header without excessBlobGas (pre-Ecotone) prices blobs at 0; the series is still there.
    header(16)->setGasLimit(30'000'000);
    header(16)->setBaseFee(900);
    auto const older = call("eth_feeHistory", params({"0x1", "0x10", Json::arrayValue}))["result"];
    BOOST_CHECK_EQUAL(older["baseFeePerBlobGas"][0].asString(), "0x0");
    BOOST_CHECK_EQUAL(older["baseFeePerBlobGas"][1].asString(), "0x0");
}

// The deposit (42000 gas) is sampled with tip 0, the 1559 tx (21000 gas) with min(20, 150 -
// 100) = 20; block gasUsed 63000: p10 -> threshold 6300 -> 0, p90 -> 56700 -> 20.
BOOST_AUTO_TEST_CASE(FeeHistoryRewardsSampleDepositsAtZero)
{
    blockWithDepositAndTx();
    auto const resp = call("eth_feeHistory", params({1, "latest", params({10.0, 90.0})}));
    BOOST_REQUIRE_MESSAGE(resp.isMember("result"), printJson(resp));
    auto const& reward = resp["result"]["reward"];
    BOOST_REQUIRE_EQUAL(reward.size(), 1U);
    BOOST_CHECK_EQUAL(reward[0][0].asString(), "0x0");
    BOOST_CHECK_EQUAL(reward[0][1].asString(), "0x14");
}

BOOST_AUTO_TEST_CASE(FeeHistoryBounds)
{
    opHeader(19, 1200, 10'000'000);
    auto const zero = call("eth_feeHistory", params({"0x0", "latest", Json::arrayValue}));
    BOOST_CHECK_EQUAL(printJson(zero["result"]), R"({"oldestBlock":"0x0"})");
    // Clamped to the 20 blocks the chain has (and never above 1024).
    auto const clamped = call("eth_feeHistory", params({"0x7d0", "latest", Json::arrayValue}));
    BOOST_CHECK_EQUAL(clamped["result"]["oldestBlock"].asString(), "0x0");
    BOOST_CHECK_EQUAL(clamped["result"]["baseFeePerGas"].size(), 21U);
    auto const beyond = call("eth_feeHistory", params({"0x1", "0x14", Json::arrayValue}));
    BOOST_CHECK_EQUAL(beyond["error"]["code"].asInt(), InvalidParams);
    Json::Value tooMany(Json::arrayValue);
    for (int i = 0; i < 101; ++i)
    {
        tooMany.append(i * 0.5);
    }
    for (auto const& bad :
        {params({50.0, 10.0}), params({101.0}), params({-1.0}), tooMany, Json::Value("x")})
    {
        auto const resp = call("eth_feeHistory", params({"0x1", "latest", bad}));
        BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), InvalidParams);
    }
}

// Element-wise equal to eth_getTransactionReceipt per tx, by tag and by block hash, and the
// deposit's OP receipt fields come through.
BOOST_AUTO_TEST_CASE(BlockReceiptsEqualPerTxReceipts)
{
    auto const hashes = blockWithDepositAndTx();
    auto const byTag = call("eth_getBlockReceipts", params({"latest"}))["result"];
    auto const byHash =
        call("eth_getBlockReceipts", params({header(19)->hash().hexPrefixed()}))["result"];
    BOOST_REQUIRE_EQUAL(byTag.size(), hashes.size());
    BOOST_CHECK(byTag == byHash);
    for (std::size_t i = 0; i < hashes.size(); ++i)
    {
        auto const single =
            call("eth_getTransactionReceipt", params({hashes[i].hexPrefixed()}))["result"];
        BOOST_CHECK_MESSAGE(byTag[Json::ArrayIndex(i)] == single, printJson(single));
    }
    BOOST_CHECK_EQUAL(byTag[0U]["type"].asString(), "0x7e");
    BOOST_CHECK_EQUAL(byTag[0U]["depositNonce"].asString(), "0x9");
    BOOST_CHECK(
        call("eth_getBlockReceipts", params({h256(0xbeefU).hexPrefixed()}))["result"].isNull());
    BOOST_CHECK(call("eth_getBlockReceipts", params({"0x14"}))["result"].isNull());  // > tip
}

// OP lane: syncing until the first forkchoice lands, then in sync, including between a
// newPayload and its forkchoiceUpdated, when the tip leads the tracked head by one.
BOOST_AUTO_TEST_CASE(SyncingFollowsForkchoiceHead)
{
    HeadEngine engine;
    auto head = engine.head;
    nodeService->engineService() = std::make_shared<engine::AnyEngineService>(std::move(engine));

    auto const beforeForkchoice = call("eth_syncing")["result"];
    BOOST_CHECK_EQUAL(beforeForkchoice["currentBlock"].asString(), "0x13");
    BOOST_CHECK_EQUAL(beforeForkchoice["highestBlock"].asString(), "0x13");

    *head = 19;
    BOOST_CHECK(call("eth_syncing")["result"] == Json::Value(false));
    *head = 18;
    BOOST_CHECK(call("eth_syncing")["result"] == Json::Value(false));
}

// pending = each sender's gapless run from its account nonce; queued = gaps and passed nonces.
BOOST_AUTO_TEST_CASE(TxpoolPendingQueuedSplit)
{
    StateStorage state;
    Address const alice("0x00000000000000000000000000000000000000a1");
    Address const bob("0x00000000000000000000000000000000000000b2");
    evmc_address aliceEvmc{};
    std::copy_n(alice.begin(), sizeof(aliceEvmc.bytes), aliceEvmc.bytes);
    ledger::account::EVMAccount aliceAccount(
        state, aliceEvmc, ledger::account::nodeAddressTableMode());
    task::syncWait(aliceAccount.create());
    task::syncWait(aliceAccount.setNonce("1"));  // bob has no account: nonce 0

    txpool::MemPoolImpl memPool;
    std::vector<protocol::Transaction::Ptr> txs;
    std::uint64_t id = 0x100;
    for (auto [who, nonce] : std::vector<std::pair<Address, uint64_t>>{
             {alice, 0}, {alice, 1}, {alice, 2}, {alice, 4}, {bob, 0}, {bob, 2}})
    {
        txs.push_back(makeTx(eip1559(nonce, 150, 20), h256(id++), who));
    }
    memPool.add(::ranges::views::all(txs));
    nodeService->setMemPool(memPool);
    nodeService->setStateStorageProvider(
        [&state]() { return std::make_shared<NodeService::StateStorage>(state); });

    auto const status = call("txpool_status")["result"];
    BOOST_CHECK_EQUAL(status["pending"].asString(), "0x3");  // alice 1, 2; bob 0
    BOOST_CHECK_EQUAL(status["queued"].asString(), "0x3");   // alice 0 (passed), 4 (gap); bob 2

    auto const content = call("txpool_content")["result"];
    auto const aliceKey = "0x" + checksummedHexAddress(alice.hex());
    auto const bobKey = "0x" + checksummedHexAddress(bob.hex());
    BOOST_CHECK(
        content["pending"][aliceKey].getMemberNames() == (std::vector<std::string>{"1", "2"}));
    BOOST_CHECK(content["pending"][bobKey].getMemberNames() == std::vector<std::string>{"0"});
    BOOST_CHECK(
        content["queued"][aliceKey].getMemberNames() == (std::vector<std::string>{"0", "4"}));
    BOOST_CHECK(content["queued"][bobKey].getMemberNames() == std::vector<std::string>{"2"});
    // eth_getTransactionByHash's shape, block fields null for a pooled tx.
    auto const& tx = content["pending"][aliceKey]["1"];
    BOOST_CHECK_EQUAL(tx["hash"].asString(), h256(0x101U).hexPrefixed());
    BOOST_CHECK_EQUAL(tx["from"].asString(), aliceKey);
    BOOST_CHECK_EQUAL(tx["nonce"].asString(), "0x1");
    BOOST_CHECK(tx["blockHash"].isNull() && tx["blockNumber"].isNull());
    BOOST_CHECK(tx["transactionIndex"].isNull());
}

BOOST_AUTO_TEST_CASE(TxpoolWithoutMempoolIsEmpty)
{
    auto const status = call("txpool_status")["result"];
    BOOST_CHECK_EQUAL(status["pending"].asString(), "0x0");
    BOOST_CHECK_EQUAL(status["queued"].asString(), "0x0");
}

// Off the OP lane the four OP-only methods answer what an unregistered method answers.
BOOST_AUTO_TEST_CASE(NonOpLaneIsMethodNotFound)
{
    auto const unregistered = call("eth_noSuchMethod")["error"];
    for (int version : {1, int(ledger::ETHEREUM_EXECUTOR_VERSION)})
    {
        setExecutorVersion(version);
        for (auto const& resp : {call("eth_feeHistory", params({"0x1", "latest"})),
                 call("eth_getBlockReceipts", params({"latest"})), call("txpool_status"),
                 call("txpool_content")})
        {
            BOOST_CHECK(resp["error"] == unregistered);
            BOOST_CHECK(!resp.isMember("result"));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
