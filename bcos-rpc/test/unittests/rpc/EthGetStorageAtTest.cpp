/*
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
 * @brief eth_getStorageAt (and the three sibling state reads) over the production storage
 *        shapes: latest answers from a forked GlobalStateStorage view (flat KV through the
 *        StateStorageProvider), historical answers from the STATE REVERSE HISTORY at the
 *        requested block (through the MPT history reader, pathdb spec §11), with explicit
 *        errors — never a latest-state fallback — when the node cannot serve that height.
 * @file EthGetStorageAtTest.cpp
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/history/HistoryCommit.h>
#include <bcos-ledger/mpt/history/HistoryRead.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

/// Wrap any state storage in an AnyStorage handle that OWNS it — the production
/// forkLatestStateView shape, inlined here because the test target does not link
/// libinitializer.
template <class Storage>
std::shared_ptr<rpc::NodeService::StateStorage> makeOwningStateStorage(Storage& storage)
{
    using AnyStateStorage = rpc::NodeService::StateStorage;
    struct Owning
    {
        std::reference_wrapper<Storage> storage;
        std::optional<AnyStateStorage> erased;

        explicit Owning(Storage& s) : storage(s) { erased.emplace(storage.get()); }
    };
    auto owner = std::make_shared<Owning>(storage);
    return {owner, std::addressof(*owner->erased)};
}

class EthGetStorageAtFixture : public RPCFixture
{
public:
    /// The MPT node row plane (the committed backend's "/mpt/" rows in production).
    using StateRowStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;
    /// The flat latest-state plane (a forked GlobalStateStorage view in production).
    using LatestStateStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

    EthGetStorageAtFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    /// Wire the two handles AirNodeInitializer wires, over one plane: in production the
    /// committed state backend holds BOTH the history rows and the current flat rows, and the
    /// "unchanged since B" answer is read from it, so the test plane has to be the same object.
    void wireHistory(bcos::protocol::BlockNumber depth = 128)
    {
        nodeService->setMPTHistoryReader(mpt::history::makeHistoryReader(m_committed));
        nodeService->setMPTHistoryDepths({.state = depth, .proof = depth});
    }

    /// Record one block's state-history entries: (row key, the value the row held BEFORE this
    /// block, or nullopt for "the row did not exist yet"). The owned buffers outlive the put,
    /// which is what HistoryEntry's non-owning views require.
    void recordStateHistory(bcos::protocol::BlockNumber block,
        std::vector<std::pair<std::string, std::optional<std::string>>> const& rows)
    {
        std::vector<bcos::executor_v1::StateKey> keys;
        std::vector<bcos::bytes> values;
        keys.reserve(rows.size());
        values.reserve(rows.size());
        std::vector<mpt::history::HistoryEntry> entries;
        entries.reserve(rows.size());
        for (auto const& [rowKey, oldValue] : rows)
        {
            keys.emplace_back(accountTable(), rowKey);
            std::optional<std::span<const bcos::byte>> view;
            if (oldValue)
            {
                values.emplace_back(oldValue->begin(), oldValue->end());
                view = std::span<const bcos::byte>(values.back());
            }
            entries.emplace_back(mpt::history::HistoryEntry{
                .key = mpt::history::historyKeyOf(keys.back()), .oldValue = view});
        }
        task::syncWait(mpt::history::StateHistoryStore::put(
            m_committed, block, entries, /*shardByteCap*/ 64 * 1024));
    }

    /// The retention boundary the commit path seeds with its first recorded block; a query reads
    /// its absence as "this node recorded nothing".
    void seedRetentionBoundary(bcos::protocol::BlockNumber oldestIntact)
    {
        task::syncWait(
            mpt::history::StateHistoryStore::writeRetentionBoundary(m_committed, oldestIntact));
    }

    /// A row as it stands NOW on the committed plane — what an unchanged-since-B key resolves to.
    void setCommittedRow(std::string const& rowKey, bcos::bytes const& value)
    {
        storage::Entry entry;
        entry.set(bcos::bytes(value));
        task::syncWait(storage2::writeOne(
            m_committed, executor_v1::StateKey{accountTable(), rowKey}, std::move(entry)));
    }

    std::string accountTable() const { return mpt::accountTableName(address); }

    /// A 32-byte word holding @p low in its last byte — the shape a storage slot row carries.
    static bcos::bytes word(bcos::byte low)
    {
        bcos::bytes value(32, 0);
        value.back() = low;
        return value;
    }

    static std::string slotRowKey(h256 const& slot)
    {
        return {reinterpret_cast<char const*>(slot.ref().data()), h256::SIZE};
    }

    /// The same 32-byte word as text, for the pre-image side of a history entry.
    static std::string wordText(bcos::byte low)
    {
        auto const value = word(low);
        return {value.begin(), value.end()};
    }

    /// The production wiring shape (AirNodeInitializer): a provider that hands back an
    /// owning AnyStorage over the latest COMMITTED-state plane, forked per request.
    void wireStateProvider()
    {
        nodeService->setStateStorageProvider(
            [this]() { return makeOwningStateStorage(m_latestState); });
    }

    /// Write a flat account-table slot row (32-byte value) into the latest-state plane.
    void setFlatSlot(bcos::h256 const& slot, bcos::bytes const& value32)
    {
        storage::Entry entry;
        entry.set(bcos::bytes(value32));
        task::syncWait(storage2::writeOne(m_latestState,
            executor_v1::StateKey{
                std::string(bcos::ledger::SYS_DIRECTORY::USER_APPS) + address.hex(),
                std::string{reinterpret_cast<char const*>(slot.ref().data()), h256::SIZE}},
            std::move(entry)));
    }

    Json::Value request(std::string const& req)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(req, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Json::Value getStorageAt(
        std::string const& addressHex, std::string const& slotHex, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getStorageAt";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        params.append(slotHex);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    Json::Value getBalance(std::string const& addressHex, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getBalance";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    Json::Value getTransactionCount(std::string const& addressHex, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getTransactionCount";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    Json::Value getCode(std::string const& addressHex, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getCode";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
    /// Stands in for the committed state backend: BOTH the history rows and the current flat
    /// rows live here, because that is the one plane a historical read touches.
    StateRowStorage m_committed;
    LatestStateStorage m_latestState;
    bcos::Address address{std::string("0x00000000000000000000000000000000000000ab")};
    h256 slotA{1U};
    h256 slotB{2U};
};

BOOST_FIXTURE_TEST_SUITE(EthGetStorageAtTest, EthGetStorageAtFixture)

/// The 32-byte padded rendering of a u256 (eth_getStorageAt's result shape).
static std::string paddedHex(bcos::u256 value)
{
    return "0x" + toHex(bcos::h256{value}.ref());
}

// Latest state, provider wired: the flat KV read must come from the committed view.
BOOST_AUTO_TEST_CASE(LatestStateFromCommittedView)
{
    wireStateProvider();
    bcos::bytes value32(32, 0);
    value32.back() = 0x2a;
    setFlatSlot(slotA, value32);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "latest");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Latest state, provider unset (tars-built NodeService): must fall back to the ledger.
BOOST_AUTO_TEST_CASE(LatestStateFallsBackToLedger)
{
    // no wireStateProvider()
    bcos::bytes value32(32, 0);
    value32.back() = 0x2a;
    storage::Entry entry;
    entry.set(bcos::bytes(value32));
    m_ledger->setStorageAt(address.hex(),
        std::string{reinterpret_cast<char const*>(slotA.ref().data()), h256::SIZE},
        std::move(entry));

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "latest");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Historical state: a slot's value at block 1, from the state reverse history. Block 2 changed
// it and recorded what it held before, so the answer at block 1 differs from the answer at the
// tip — the discriminator a latest-state read would fail.
BOOST_AUTO_TEST_CASE(HistoricalSlotFromStateHistory)
{
    wireHistory();
    wireStateProvider();
    seedRetentionBoundary(0);
    setCommittedRow(slotRowKey(slotA), word(0x63));  // the value NOW
    setFlatSlot(slotA, word(0x63));
    recordStateHistory(2, {{slotRowKey(slotA), wordText(0x2a)}});

    auto historical = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!historical.isMember("error"));
    BOOST_REQUIRE(historical.isMember("result"));
    BOOST_TEST(historical["result"].asString() == paddedHex(42));

    auto latest = getStorageAt(address.hexPrefixed(), "0x1", "latest");
    BOOST_REQUIRE(latest.isMember("result"));
    BOOST_TEST(latest["result"].asString() == paddedHex(0x63));
}

// A row nothing changed after the queried block keeps its current value — the HistoryUseCurrent
// arm, read from the committed plane so a not-yet-committed block cannot leak in.
BOOST_AUTO_TEST_CASE(HistoricalSlotUnchangedSinceReadsTheCurrentRow)
{
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow(slotRowKey(slotB), word(0x11));
    // Block 2 changed a DIFFERENT row, so slot B has no index row after block 1.
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});

    auto resp = getStorageAt(address.hexPrefixed(), "0x2", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(0x11));
}

// Absence has ONE reading now. The MPT path had to tell "the slot is genuinely unset" from "the
// slot never entered a scenario-A trie", and answered the second with an explicit error or with
// a flat fallback that ignored the requested height. The index records what the ROW held, so a
// slot that did not exist at block 1 reads zero on either scenario — like the latest path.
BOOST_AUTO_TEST_CASE(HistoricalAbsentSlotReadsZero)
{
    wireHistory();
    seedRetentionBoundary(0);
    // Block 2 CREATED the slot: its pre-image is "did not exist".
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});
    setCommittedRow(slotRowKey(slotA), word(0x2a));

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(0));
}

// An account with no rows at the queried block reads as an empty account across all four
// endpoints — 0 balance, 0 nonce, empty code, zero slots. No "dormant in scenario A" error
// remains: that error existed because an incomplete TRIE could not tell dormant from absent,
// and the row index has no such blind spot.
BOOST_AUTO_TEST_CASE(HistoricalAbsentAccountReadsEmpty)
{
    wireHistory();
    seedRetentionBoundary(0);
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});

    std::string const absent = "0x00000000000000000000000000000000000000cc";
    auto slot = getStorageAt(absent, "0x1", "0x1");
    BOOST_TEST(!slot.isMember("error"));
    BOOST_TEST(slot["result"].asString() == paddedHex(0));
    auto balance = getBalance(absent, "0x1");
    BOOST_TEST(!balance.isMember("error"));
    BOOST_TEST(balance["result"].asString() == toQuantity(0));
    auto nonce = getTransactionCount(absent, "0x1");
    BOOST_TEST(!nonce.isMember("error"));
    BOOST_TEST(nonce["result"].asString() == toQuantity(0));
    auto code = getCode(absent, "0x1");
    BOOST_TEST(!code.isMember("error"));
    BOOST_TEST(code["result"].asString() == std::string("0x"));
}

// A system-contract address is refused, not answered with zeros. EVMAccount stores those under
// "/sys/", and the state history captures only the "/apps/" rows Classify.h::parseAccountTable
// recognises — the set MPTBuilder folds into the Ethereum commitment — so a system contract's
// rows were never recorded at any height. Looking one up under "/apps/" would find nothing and
// report 0 / 0 / "0x" / a zero word for state that demonstrably exists, which is exactly the
// fabricated answer G6 forbids; the honest answer is that this node cannot serve it.
BOOST_AUTO_TEST_CASE(HistoricalSystemContractAddressReturns32004)
{
    wireHistory();
    seedRetentionBoundary(0);
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});

    // 0x1000 is in c_systemTxsAddress, the same set the latest branch routes to "/sys/".
    std::string const systemContract = "0x0000000000000000000000000000000000001000";
    for (auto const& [method, resp] :
        {std::make_pair("eth_getStorageAt", getStorageAt(systemContract, "0x1", "0x1")),
            std::make_pair("eth_getBalance", getBalance(systemContract, "0x1")),
            std::make_pair("eth_getTransactionCount", getTransactionCount(systemContract, "0x1")),
            std::make_pair("eth_getCode", getCode(systemContract, "0x1"))})
    {
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), method);
        BOOST_CHECK_MESSAGE(resp["error"]["code"].asInt() == -32004, method);
        BOOST_CHECK_MESSAGE(
            resp["error"]["message"].asString().find("system-contract") != std::string::npos,
            method);
    }

    // A user address at the same height still answers, so the refusal is about the address and
    // not about the fixture.
    auto ok = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!ok.isMember("error"));
}

// The refusal above must survive an UNPADDED address. JSON-RPC callers may send "0x1000" for
// 0x…001000, and the endpoints reach the table helper with whatever arrived minus "0x" and
// lowercased, while c_systemTxsAddress holds 40-char forms — so testing membership before
// normalizing would let exactly the short form through and answer it from "/apps/00…001000", a
// table with no rows. The zero is fabricated either way; the short form is just the spelling
// that looks least like a system address.
BOOST_AUTO_TEST_CASE(HistoricalShortFormAddressIsNormalizedBeforeTheSystemCheck)
{
    wireHistory();
    seedRetentionBoundary(0);
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});
    setCommittedRow("balance", bcos::bytes{'7', '7'});

    // A system contract in short form is refused, exactly as its padded spelling is.
    auto const shortSystem = getBalance("0x1000", "0x1");
    BOOST_REQUIRE(shortSystem.isMember("error"));
    BOOST_CHECK_EQUAL(shortSystem["error"]["code"].asInt(), -32004);
    BOOST_CHECK(
        shortSystem["error"]["message"].asString().find("system-contract") != std::string::npos);
    auto const paddedSystem = getBalance("0x0000000000000000000000000000000000001000", "0x1");
    BOOST_REQUIRE(paddedSystem.isMember("error"));
    BOOST_CHECK_EQUAL(paddedSystem["error"]["code"].asInt(), -32004);

    // A USER address in short form must still answer, and answer the same as its padded form —
    // normalizing must not turn every short address into a refusal.
    auto const paddedUser = getBalance(address.hexPrefixed(), "0x1");
    BOOST_TEST(!paddedUser.isMember("error"));
    std::string shortUser = address.hexPrefixed();
    auto const firstSignificant = shortUser.find_first_not_of('0', 2);
    BOOST_REQUIRE(firstSignificant != std::string::npos);
    shortUser = "0x" + shortUser.substr(firstSignificant);
    BOOST_TEST_MESSAGE("short user form: " + shortUser);
    auto const shortUserResp = getBalance(shortUser, "0x1");
    BOOST_TEST(!shortUserResp.isMember("error"));
    BOOST_CHECK_EQUAL(shortUserResp["result"].asString(), paddedUser["result"].asString());
}

// No history reader wired (a tars-built NodeService has no local storage): -32603, never a
// silent latest answer. A deployment fault, not a request one.
BOOST_AUTO_TEST_CASE(HistoricalWithoutHistoryReaderReturns32603)
{
    // no wireHistory()
    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(resp["error"]["message"].asString().find("MPT not enabled") != std::string::npos);
}

// The node retains no state history at all (storage.mpt_history_state_blocks = 0): every
// historical state read is refused, because the current rows are not an answer for an old block.
BOOST_AUTO_TEST_CASE(HistoricalWithZeroDepthReturns32004)
{
    wireHistory(/*depth*/ 0);
    seedRetentionBoundary(0);
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});

    for (auto const& [method, resp] :
        {std::make_pair("eth_getStorageAt", getStorageAt(address.hexPrefixed(), "0x1", "0x1")),
            std::make_pair("eth_getBalance", getBalance(address.hexPrefixed(), "0x1")),
            std::make_pair(
                "eth_getTransactionCount", getTransactionCount(address.hexPrefixed(), "0x1")),
            std::make_pair("eth_getCode", getCode(address.hexPrefixed(), "0x1"))})
    {
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), method);
        BOOST_CHECK_MESSAGE(resp["error"]["code"].asInt() == -32004, method);
        BOOST_CHECK_MESSAGE(
            resp["error"]["message"].asString().find("not retained") != std::string::npos, method);
    }
}

// Out of window: latest is 19, a depth of 2 retains blocks 18-19, so block 1 predates the
// window. -32004, not an answer assembled from today's rows (spec B.3, G5).
BOOST_AUTO_TEST_CASE(HistoricalOutOfWindowReturns32004)
{
    wireHistory(/*depth*/ 2);
    seedRetentionBoundary(0);
    recordStateHistory(2, {{slotRowKey(slotA), std::nullopt}});

    for (auto const& [method, resp] :
        {std::make_pair("eth_getStorageAt", getStorageAt(address.hexPrefixed(), "0x1", "0x1")),
            std::make_pair("eth_getBalance", getBalance(address.hexPrefixed(), "0x1")),
            std::make_pair(
                "eth_getTransactionCount", getTransactionCount(address.hexPrefixed(), "0x1")),
            std::make_pair("eth_getCode", getCode(address.hexPrefixed(), "0x1"))})
    {
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), method);
        BOOST_CHECK_MESSAGE(resp["error"]["code"].asInt() == -32004, method);
        BOOST_CHECK_MESSAGE(
            resp["error"]["message"].asString().find("history window") != std::string::npos,
            method);
    }
}

// An era this node never recorded is refused too, and for a different reason than an expired
// one: with no rows for the blocks after B, every key would resolve to "unchanged since B" and
// hand back today's state under block B's number.
BOOST_AUTO_TEST_CASE(HistoricalWithoutRecordedEraReturns32004)
{
    wireHistory();
    // No boundary row and no manifests: nothing was ever recorded here.
    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("No state history recorded") != std::string::npos);
}

// Spec: the result is a fixed 32-byte DATA. The flat (latest) path must left-pad a narrower
// stored row to a full word, exactly like geth's common.BytesToHash(value).Hex().
BOOST_AUTO_TEST_CASE(LatestStatePadsNarrowStoredValue)
{
    wireStateProvider();
    // Store a 1-byte value; the RPC output must still be 32 bytes.
    setFlatSlot(slotA, bcos::bytes({0x2a}));

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "latest");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Spec: a QUANTITY position is at most 256 bits (32 bytes). A wider position is an invalid
// param (-32602), surfaced as a JSON-RPC error instead of an uncaught exception.
BOOST_AUTO_TEST_CASE(OverwidePositionReturnsInvalidParams)
{
    std::string overwide = "0x" + std::string(66, '1');  // 33 bytes

    auto resp = getStorageAt(address.hexPrefixed(), overwide, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
    BOOST_CHECK(resp["error"]["message"].asString().find("storage position") != std::string::npos);
}

// blockTag semantics: the default depths are 0 — PBFT commits are final, so safe/finalized
// equal "latest" (flat path), byte-identical to pre-upgrade behaviour.
BOOST_AUTO_TEST_CASE(DefaultSafeFinalizedStayOnLatest)
{
    wireStateProvider();
    bcos::bytes value32(32, 0);
    value32.back() = 0x2a;
    setFlatSlot(slotA, value32);

    for (auto const& tag : {std::string("safe"), std::string("finalized")})
    {
        auto resp = getStorageAt(address.hexPrefixed(), "0x1", tag);
        BOOST_TEST_MESSAGE("tag " + tag);
        BOOST_TEST(!resp.isMember("error"));
        BOOST_REQUIRE(resp.isMember("result"));
        BOOST_TEST(resp["result"].asString() == paddedHex(42));
    }
}

// blockTag semantics: with a configured safeBlockDepth, "safe" resolves to latest - depth (a
// committed historical block served from the reverse history, never the head). latest = 19,
// depth 1 -> safe = block 18, where the slot still held its pre-block-19 value.
BOOST_AUTO_TEST_CASE(SafeTagResolvesToHistoricalState)
{
    nodeService->setSafeBlockDepth(1);
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow(slotRowKey(slotA), word(0x63));
    recordStateHistory(19, {{slotRowKey(slotA), wordText(0x2a)}});

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "safe");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// The same, one block deeper: finalizedBlockDepth 2 -> "finalized" = block 17.
BOOST_AUTO_TEST_CASE(FinalizedTagResolvesToHistoricalState)
{
    nodeService->setFinalizedBlockDepth(2);
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow(slotRowKey(slotA), word(0x63));
    recordStateHistory(18, {{slotRowKey(slotA), wordText(0x2a)}});

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "finalized");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// The safe/finalized depths are configurable: safeBlockDepth = 2 -> "safe" = block 17.
BOOST_AUTO_TEST_CASE(ConfigurableSafeDepth)
{
    nodeService->setSafeBlockDepth(2);
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow(slotRowKey(slotA), word(0x63));
    recordStateHistory(18, {{slotRowKey(slotA), wordText(0x2a)}});

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "safe");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Historical getBalance: the balance row as of block 1, which block 2 overwrote. The tip reads
// the new value, so a latest-state answer would be visibly wrong.
BOOST_AUTO_TEST_CASE(HistoricalBalanceFromStateHistory)
{
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow("balance", bcos::bytes{'9', '9', '9', '9'});
    recordStateHistory(2, {{std::string("balance"), std::string("1000")}});

    auto resp = getBalance(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toQuantity(1000));
}

// Historical getTransactionCount: same shape, on the nonce row.
BOOST_AUTO_TEST_CASE(HistoricalNonceFromStateHistory)
{
    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow("nonce", bcos::bytes{'9'});
    recordStateHistory(2, {{std::string("nonce"), std::string("7")}});

    auto resp = getTransactionCount(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toQuantity(7));
}

// Historical getCode: the codeHash ROW comes from the reverse history; the code BYTES do not
// need one — s_code_binary is content-addressed and append-only, so the bytes under a hash are
// the same at every height. Block 2 replaced the code, and block 1 must still return the old.
BOOST_AUTO_TEST_CASE(HistoricalCodeFromStateHistory)
{
    bcos::bytes const oldCode{0x60, 0x00, 0x60, 0x00};
    bcos::bytes const newCode{0x60, 0x01};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 oldHash;
    bcos::h256 newHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(oldCode), oldHash);
    bcos::crypto::hasher::hash(hasher, bcos::ref(newCode), newHash);

    auto const stateStorage = m_ledger->getStateStorage();
    for (auto const& [hash, code] :
        std::vector<std::pair<bcos::h256, bcos::bytes>>{{oldHash, oldCode}, {newHash, newCode}})
    {
        storage::Entry codeEntry;
        codeEntry.set(bcos::bytes(code));
        task::syncWait(storage2::writeOne(*stateStorage,
            executor_v1::StateKey{bcos::ledger::SYS_CODE_BINARY, hash.toRawString()},
            std::move(codeEntry)));
    }

    wireHistory();
    seedRetentionBoundary(0);
    setCommittedRow("codeHash", bcos::bytes(newHash.begin(), newHash.end()));
    recordStateHistory(2, {{std::string("codeHash"), oldHash.toRawString()}});

    auto resp = getCode(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toHexStringWithPrefix(oldCode));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
