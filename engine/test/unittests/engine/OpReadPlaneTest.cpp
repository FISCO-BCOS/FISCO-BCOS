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
 * @file OpReadPlaneTest.cpp
 * @brief The OP lane's RPC read plane over the unfinalized window (D1 §10.2): EthEndpoint
 *        over a real OpEngineService + OpScheduler<MLS> + Ledger, through OpCanonicalReaderImpl.
 *
 * Acceptance (ticket 06, RPC side): after newPayload(B2a), newPayload(B2b), FCU(head=B2b) every
 * eth_* read describes B2b; after FCU(head=B2a) they describe B2a; after FCU(head=B1) `latest`
 * is block 1. `finalized` is always the backend tip, `safe` the tracker's. A side-branch block
 * is readable by hash, never by number; a side-branch-only transaction is null. After a restart
 * latest == finalized.
 */

#include "OpReorgFixture.h"
#include <bcos-framework/engine/OpCanonicalReader.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/web3jsonrpc/Web3FilterSystem.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/CanonicalReads.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <engine/bcos-engine/OpCanonicalReaderImpl.h>
#include <json/json.h>
#include <boost/asio/io_context.hpp>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace op_read_plane
{
using namespace op_engine_reorg;
using Reader = bcos::engine::OpCanonicalReaderImpl<OpEngine, Delegate>;

/// A GroupManager that serves exactly one NodeService: what the filter system needs to route
/// eth_getLogs back to the same ledger + facade the endpoint reads.
struct SingleNodeGroupManager : bcos::rpc::GroupManager
{
    explicit SingleNodeGroupManager(bcos::rpc::NodeService::Ptr node)
      : GroupManager("chain0"), m_node(std::move(node))
    {}
    bcos::rpc::NodeService::Ptr getNodeService(std::string_view, std::string_view) const override
    {
        return m_node;
    }
    bcos::rpc::NodeService::Ptr m_node;
};

/// The ReorgFixture plus the production wiring EngineServiceInitializer::buildOp performs
/// (head provider, chain-view provider, facade) and an EthEndpoint over it.
struct ReadPlaneFixture
{
    ReorgFixture f;
    std::shared_ptr<Reader> reader;
    bcos::rpc::NodeService::Ptr nodeService;
    boost::asio::io_context io;
    std::shared_ptr<bcos::rpc::Web3FilterSystem> filters;
    std::unique_ptr<bcos::rpc::EthEndpoint> eth;

    ReadPlaneFixture()
    {
        seedExecutorVersion();
        wire();
    }

    /// The OP lane is decided by the executor_version SYS_CONFIG row (scenario B: complete
    /// tries, MPT-only account state); Ledger::buildGenesisBlock writes it in production.
    void seedExecutorVersion()
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(bcos::ledger::SystemConfigEntry{
            std::to_string(bcos::ledger::OPSTACK_EXECUTOR_VERSION), 0}));
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, StateKey{bcos::ledger::SYS_CONFIG, "executor_version"}, std::move(entry)));
        bcos::storage::Entry table;
        table.set(std::string(bcos::ledger::SYS_VALUE));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_TABLES, std::string(bcos::ledger::SYS_CONFIG)},
            std::move(table)));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }

    /// Same three hooks as EngineServiceInitializer::buildOp, over the fixture's fresh
    /// service/delegate pair (called again after restart()).
    void wire()
    {
        auto* service = f.service.get();
        f.opDelegate->setCanonicalHeadProvider([service]() -> std::optional<bcos::h256> {
            auto head = service->trackedHead();
            return head ? std::optional(head->hash) : std::nullopt;
        });
        f.service->setChainViewProvider(
            [delegate = f.opDelegate](bcos::h256 const& hash) { return delegate->viewAt(hash); });
        // Non-owning: the fixture's unique_ptr owns the service and outlives the reader (the
        // production wiring aliases the composition root's holder instead).
        reader = std::make_shared<Reader>(
            std::shared_ptr<OpEngine>(std::shared_ptr<void>{}, f.service.get()), f.opDelegate,
            f.blockFactory);

        nodeService = std::make_shared<bcos::rpc::NodeService>(
            f.ledger, f.opDelegate, nullptr, nullptr, nullptr, f.blockFactory, nullptr);
        nodeService->setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
        nodeService->setMPTNodeReader(bcos::storage2::makeMPTNodeReader(f.backendStorage));
        nodeService->setOpCanonicalReader(reader);
        filters = std::make_shared<bcos::rpc::Web3FilterSystem>(
            io, std::make_shared<SingleNodeGroupManager>(nodeService), "group0", 60, 10);
        eth = std::make_unique<bcos::rpc::EthEndpoint>(nodeService, filters, false);
    }

    void restart()
    {
        f.restart();
        wire();
    }

    // ---- JSON-RPC drivers (EthEndpoint methods take the params array, fill the response) ----

    template <class Method>
    Json::Value call(Method method, Json::Value params)
    {
        Json::Value response;
        bcos::task::syncWait((eth.get()->*method)(params, response));
        return response;
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
    static std::string hex(evmc::address const& a)
    {
        return bcos::toHexStringWithPrefix(bcos::bytesConstRef(a.bytes, sizeof(a.bytes)));
    }

    std::string blockNumber()
    {
        return call(&bcos::rpc::EthEndpoint::blockNumber, params({}))["result"].asString();
    }
    Json::Value blockByNumber(std::string const& tag, bool full)
    {
        return call(&bcos::rpc::EthEndpoint::getBlockByNumber, params({tag, full}))["result"];
    }
    Json::Value blockByHash(bcos::h256 const& hash, bool full)
    {
        return call(
            &bcos::rpc::EthEndpoint::getBlockByHash, params({hash.hexPrefixed(), full}))["result"];
    }
    Json::Value receipt(bcos::h256 const& txHash)
    {
        return call(&bcos::rpc::EthEndpoint::getTransactionReceipt,
            params({txHash.hexPrefixed()}))["result"];
    }
    Json::Value transaction(bcos::h256 const& txHash)
    {
        return call(&bcos::rpc::EthEndpoint::getTransactionByHash,
            params({txHash.hexPrefixed()}))["result"];
    }
    std::string balance(std::string const& tag)
    {
        return call(&bcos::rpc::EthEndpoint::getBalance, params({hex(kAccount), tag}))["result"]
            .asString();
    }
    std::string nonce(std::string const& tag)
    {
        return call(
            &bcos::rpc::EthEndpoint::getTransactionCount, params({hex(kAccount), tag}))["result"]
            .asString();
    }
    Json::Value proof(std::string const& tag)
    {
        return call(&bcos::rpc::EthEndpoint::getProof,
            params({hex(kAccount), Json::Value(Json::arrayValue), tag}))["result"];
    }
    Json::Value logs(std::string const& from, std::string const& to)
    {
        Json::Value filter(Json::objectValue);
        filter["fromBlock"] = from;
        filter["toBlock"] = to;
        filter["address"] = Json::Value(Json::arrayValue);
        filter["topics"] = Json::Value(Json::arrayValue);
        return call(&bcos::rpc::EthEndpoint::getLogs, params({filter}))["result"];
    }
    Json::Value blockReceipts(Json::Value const& id)
    {
        return call(&bcos::rpc::EthEndpoint::getBlockReceipts, params({id}))["result"];
    }
    Json::Value ethCall(std::string const& tag)
    {
        Json::Value tx(Json::objectValue);
        tx["to"] = hex(kSink);
        tx["data"] = "0x";
        using TwoArg =
            bcos::task::Task<void> (bcos::rpc::EthEndpoint::*)(const Json::Value&, Json::Value&);
        return call(static_cast<TwoArg>(&bcos::rpc::EthEndpoint::call), params({tx, tag}));
    }
    std::string code(std::string const& tag)
    {
        return call(&bcos::rpc::EthEndpoint::getCode, params({hex(kSink), tag}))["result"]
            .asString();
    }
    std::string storageAt(std::string const& tag)
    {
        return call(
            &bcos::rpc::EthEndpoint::getStorageAt, params({hex(kSink), "0x0", tag}))["result"]
            .asString();
    }

    /// Every acceptance read at once: `latest` and its number describe @p head with balance
    /// @p balanceHex, and its mint transaction resolves to it.
    void expectHead(bcos::h256 const& head, bcos::protocol::BlockNumber number,
        std::string const& balanceHex, bcos::h256 const& mintTx)
    {
        auto const numberTag = toQuantity(number);
        BOOST_CHECK_EQUAL(blockNumber(), numberTag);
        for (auto const& tag : {std::string("latest"), numberTag})
        {
            for (bool full : {false, true})
            {
                auto block = blockByNumber(tag, full);
                BOOST_REQUIRE_MESSAGE(block.isObject(), "eth_getBlockByNumber(" << tag << ")");
                BOOST_CHECK_EQUAL(block["hash"].asString(), head.hexPrefixed());
                BOOST_CHECK_EQUAL(block["number"].asString(), numberTag);
            }
            BOOST_CHECK_EQUAL(balance(tag), balanceHex);
            auto p = proof(tag);
            BOOST_REQUIRE(p.isObject());
            BOOST_CHECK_EQUAL(p["balance"].asString(), balanceHex);
        }
        auto byHash = blockByHash(head, true);
        BOOST_REQUIRE(byHash.isObject());
        BOOST_CHECK_EQUAL(byHash["number"].asString(), numberTag);
        auto r = receipt(mintTx);
        BOOST_REQUIRE_MESSAGE(r.isObject(), "receipt of the head's mint tx");
        BOOST_CHECK_EQUAL(r["blockHash"].asString(), head.hexPrefixed());
        BOOST_CHECK_EQUAL(r["blockNumber"].asString(), numberTag);
        auto t = transaction(mintTx);
        BOOST_REQUIRE(t.isObject());
        BOOST_CHECK_EQUAL(t["blockHash"].asString(), head.hexPrefixed());
        auto rs = blockReceipts(numberTag);
        BOOST_REQUIRE(rs.isArray());
        BOOST_CHECK_EQUAL(rs.size(), 2U);  // L1-attributes deposit + mint
        BOOST_CHECK_EQUAL(rs[1U]["blockHash"].asString(), head.hexPrefixed());
        BOOST_CHECK_EQUAL(nonce("latest"), nonce(numberTag));
        BOOST_CHECK(!ethCall("latest").isMember("error"));
        BOOST_CHECK_EQUAL(code("latest"), "0x");
        BOOST_CHECK_EQUAL(storageAt("latest"),
            "0x0000000000000000000000000000000000000000000000000000000000000000");
        auto l = logs(numberTag, numberTag);
        BOOST_CHECK(l.isArray());  // block found and scanned; deposits here emit no logs
    }
};

}  // namespace op_read_plane

BOOST_AUTO_TEST_SUITE(OpReadPlaneTest)

using namespace op_read_plane;

// The facade alone (D1 §10.2 rules): heights from the tracker, views from the scheduler.
BOOST_AUTO_TEST_CASE(facade_follows_tracker_head_and_backend_finalized)
{
    ReadPlaneFixture t;
    auto c = setupB1B2a(t.f);
    auto s = deriveB2b(t.f, c);
    auto& reader = *t.reader;

    auto head = bcos::task::syncWait(reader.head());
    BOOST_CHECK_EQUAL(head.hash.hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(head.number, 2);
    auto fin = bcos::task::syncWait(reader.finalized());
    BOOST_CHECK_EQUAL(fin.hash.hex(), c.h1.hex());
    BOOST_CHECK_EQUAL(fin.number, 1);
    BOOST_CHECK_EQUAL(bcos::task::syncWait(reader.safeNumber()), 1);
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(reader.canonicalHashAt(2)).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(reader.canonicalHashAt(1)).value_or(bcos::h256{}).hex(), c.h1.hex());
    BOOST_CHECK(!bcos::task::syncWait(reader.canonicalHashAt(3)).has_value());
    BOOST_CHECK_EQUAL(reader.unfinalizedNumberOf(c.h2a).value_or(-1), 2);
    BOOST_CHECK_EQUAL(reader.unfinalizedNumberOf(s.h2b).value_or(-1), 2);
    BOOST_CHECK(!reader.unfinalizedNumberOf(c.h1).has_value());
    // The side branch's block is readable by hash from its own layer.
    auto sideBlock = bcos::task::syncWait(reader.unfinalizedBlock(
        c.h2a, bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS | bcos::ledger::RECEIPTS));
    BOOST_REQUIRE(sideBlock);
    BOOST_CHECK_EQUAL(sideBlock->blockHeader()->number(), 2);
    BOOST_CHECK_EQUAL(sideBlock->transactionsSize(), 2U);
    BOOST_CHECK_EQUAL(sideBlock->receiptsSize(), 2U);
    BOOST_CHECK(!bcos::task::syncWait(reader.unfinalizedBlock(c.h1, bcos::ledger::HEADER)));
    // Transactions: the head chain's yes, the side branch's no.
    auto onHead = bcos::task::syncWait(reader.transactionOnHeadChain(t.f.mintTxHash(s.b2b)));
    BOOST_REQUIRE(onHead.has_value());
    BOOST_CHECK_EQUAL(onHead->blockHash.hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(onHead->receipt->blockNumber(), 2);
    BOOST_CHECK(!bcos::task::syncWait(reader.transactionOnHeadChain(t.f.mintTxHash(c.b2a))));
    // Readers over chain views: the head's and the sibling's, none for an unknown hash.
    BOOST_CHECK(bcos::task::syncWait(reader.mptNodeReaderAt(s.h2b)) != nullptr);
    BOOST_CHECK(bcos::task::syncWait(reader.stateStorageAt(c.h2a)) != nullptr);
    BOOST_CHECK(bcos::task::syncWait(reader.mptNodeReaderAt(c.h1)) != nullptr);  // finalized tip
    BOOST_CHECK(
        bcos::task::syncWait(reader.mptNodeReaderAt(bcos::h256(std::string(64, 'f')))) == nullptr);
}

// The acceptance scenario on the RPC surface: B2b, then B2a, then B1 as head.
BOOST_AUTO_TEST_CASE(rpc_reads_follow_forkchoice_both_ways)
{
    ReadPlaneFixture t;
    auto c = setupB1B2a(t.f);
    auto s = deriveB2b(t.f, c);
    auto const mintA = t.f.mintTxHash(c.b2a);
    auto const mintB = t.f.mintTxHash(s.b2b);

    // head = B2b: everything describes B2b (bal 15).
    t.expectHead(s.h2b, 2, "0xf", mintB);
    // finalized = backend tip (B1, bal 10), safe = tracker (B1).
    BOOST_CHECK_EQUAL(t.blockByNumber("finalized", false)["hash"].asString(), c.h1.hexPrefixed());
    BOOST_CHECK_EQUAL(t.blockByNumber("safe", false)["hash"].asString(), c.h1.hexPrefixed());
    BOOST_CHECK_EQUAL(t.balance("finalized"), "0xa");
    BOOST_CHECK_EQUAL(t.balance("0x1"), "0xa");
    BOOST_CHECK_EQUAL(t.proof("finalized")["balance"].asString(), "0xa");
    // The replaced sibling: by hash yes (header, txs, receipts), by number never; its tx null.
    auto side = t.blockByHash(c.h2a, true);
    BOOST_REQUIRE(side.isObject());
    BOOST_CHECK_EQUAL(side["number"].asString(), "0x2");
    BOOST_CHECK_EQUAL(side["transactions"].size(), 2U);
    auto sideReceipts = t.blockReceipts(c.h2a.hexPrefixed());
    BOOST_REQUIRE(sideReceipts.isArray());
    BOOST_CHECK_EQUAL(sideReceipts.size(), 2U);
    BOOST_CHECK_EQUAL(sideReceipts[1U]["blockHash"].asString(), c.h2a.hexPrefixed());
    BOOST_CHECK(t.receipt(mintA).isNull());
    BOOST_CHECK(t.transaction(mintA).isNull());
    // eth_getProof by the sibling's hash proves against ITS state (bal 20).
    BOOST_CHECK_EQUAL(t.proof(c.h2a.hexPrefixed())["balance"].asString(), "0x14");
    // Above the head: nothing.
    BOOST_CHECK(t.blockByNumber("0x3", false).isNull());

    // FCU(head=B2a): the backup restore (§11.3). Everything describes B2a (bal 20).
    BOOST_REQUIRE(
        t.f.fcu(t.f.fc(c.h2a, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    t.expectHead(c.h2a, 2, "0x14", mintA);
    BOOST_CHECK(t.receipt(mintB).isNull());
    BOOST_CHECK(t.transaction(mintB).isNull());
    BOOST_CHECK(t.blockByHash(s.h2b, false).isObject());

    // FCU(head=B1): latest is block 1; both siblings stay readable by hash only.
    BOOST_REQUIRE(
        t.f.fcu(t.f.fc(c.h1, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(t.blockNumber(), "0x1");
    BOOST_CHECK_EQUAL(t.blockByNumber("latest", false)["hash"].asString(), c.h1.hexPrefixed());
    BOOST_CHECK(t.blockByNumber("0x2", false).isNull());
    BOOST_CHECK_EQUAL(t.balance("latest"), "0xa");
    BOOST_CHECK_EQUAL(t.proof("latest")["balance"].asString(), "0xa");
    BOOST_CHECK(t.blockByHash(c.h2a, false).isObject());
    BOOST_CHECK(t.blockByHash(s.h2b, false).isObject());
    BOOST_CHECK(t.receipt(mintA).isNull());
    BOOST_CHECK(t.receipt(mintB).isNull());
    // The finalized block's transaction is the ledger's, whatever the head.
    BOOST_CHECK_EQUAL(t.receipt(t.f.mintTxHash(c.b1))["blockHash"].asString(), c.h1.hexPrefixed());
    BOOST_CHECK(t.logs("0x1", "0x1").isArray());
    BOOST_CHECK(t.blockByNumber("0x2", true).isNull());
}

// D1 §13.2: after a restart the three tags agree on the finalized tip until op-node's first
// forkchoiceUpdated, and the finalized part of the old head chain is served by the ledger.
BOOST_AUTO_TEST_CASE(restart_answers_finalized_for_every_tag)
{
    ReadPlaneFixture t;
    auto c = setupB1B2a(t.f);
    auto s = deriveB2b(t.f, c);
    // Finalize B2b (head stays B2b).
    BOOST_REQUIRE(t.f.fcu(t.f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status ==
                  PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(t.f.backendNumber(), 2);

    t.restart();
    BOOST_CHECK(!t.f.trackedHead().has_value());
    auto head = bcos::task::syncWait(t.reader->head());
    auto fin = bcos::task::syncWait(t.reader->finalized());
    BOOST_CHECK_EQUAL(head.hash.hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(head.number, 2);
    BOOST_CHECK_EQUAL(fin.hash.hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(bcos::task::syncWait(t.reader->safeNumber()), 2);
    BOOST_CHECK_EQUAL(t.blockNumber(), "0x2");
    for (auto const* tag : {"latest", "safe", "finalized"})
    {
        auto block = t.blockByNumber(tag, true);
        BOOST_REQUIRE_MESSAGE(block.isObject(), tag);
        BOOST_CHECK_EQUAL(block["hash"].asString(), s.h2b.hexPrefixed());
    }
    BOOST_CHECK_EQUAL(t.balance("latest"), "0xf");
    BOOST_CHECK_EQUAL(t.proof("latest")["balance"].asString(), "0xf");
    // The pruned sibling is gone; the finalized head's tx is the ledger's.
    BOOST_CHECK(t.blockByHash(c.h2a, false).isNull());
    BOOST_CHECK_EQUAL(
        t.receipt(t.f.mintTxHash(s.b2b))["blockHash"].asString(), s.h2b.hexPrefixed());
    // op-node's restart FCU (head = safe = finalized = tip) is VALID and changes nothing.
    BOOST_REQUIRE(t.f.fcu(t.f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status ==
                  PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(t.blockNumber(), "0x2");
}

// The scheduler's own reads (eth_call / getCode / getPendingStorageAt) and the payload-build
// seal view follow the tracker head through the wiring, not the finalized plane.
BOOST_AUTO_TEST_CASE(scheduler_reads_and_seal_view_follow_the_head)
{
    ReadPlaneFixture t;
    auto c = setupB1B2a(t.f);
    auto s = deriveB2b(t.f, c);

    // getPendingStorageAt reads the head chain (nonce of the depositor advanced by B2b's mint;
    // deposits from kAccount bump its nonce, so the head chain's value exceeds the finalized
    // one).
    bcos::Address account;
    std::memcpy(account.data(), kAccount.bytes, sizeof(kAccount.bytes));
    auto pendingNonce = [&] {
        auto entry = bcos::task::syncWait(t.f.opDelegate->getPendingStorageAt(
            account.hex(), bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE, 0));
        BOOST_REQUIRE(entry.has_value());
        return std::string(entry->get());
    };
    auto const headNonce = pendingNonce();
    BOOST_REQUIRE(
        t.f.fcu(t.f.fc(c.h1, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto const finalizedNonce = pendingNonce();
    BOOST_CHECK_NE(headNonce, finalizedNonce);
    BOOST_CHECK_EQUAL(t.nonce("latest"), toQuantity(bcos::u256(finalizedNonce)));
    BOOST_REQUIRE(
        t.f.fcu(t.f.fc(s.h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(pendingNonce(), headNonce);
    BOOST_CHECK_EQUAL(t.nonce("latest"), toQuantity(bcos::u256(headNonce)));

    // A build on B2b (sequencer lane, noTxPool=false) seals against B2b's chain: the view's
    // current number is 2, not the finalized 1.
    std::vector<bcos::protocol::BlockNumber> sealedAt;
    t.f.memPool.onSeal = [&](bcos::protocol::BlockNumber n) { sealedAt.push_back(n); };
    auto attributes = ReorgFixture::attrs(3, 1);
    attributes.noTxPool = false;
    auto built = t.f.fcu(t.f.fc(s.h2b, c.h1, c.h1), &attributes);
    BOOST_REQUIRE(built.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(sealedAt.size(), 1U);
    BOOST_CHECK_EQUAL(sealedAt.front(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
