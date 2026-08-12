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
 * @brief eth_getStorageAt over the production storage shapes: latest answers from a forked
 *        GlobalStateStorage view (flat KV through the StateStorageProvider), historical
 *        answers from the MPT at the block's committed state root (through the MPT node
 *        reader), with explicit errors when the root is unavailable.
 * @file EthGetStorageAtTest.cpp
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/MPTNodeReadStorage.h>
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

    /// Commit @p entries into the trie whose nodes live as "/mpt/" STATE ROWS (same helper
    /// as EthGetProofReaderWiringTest).
    bcos::h256 commitIntoStateRows(std::map<bcos::h256, bcos::bytes> const& entries)
    {
        std::map<bcos::h256, std::optional<bcos::bytes>> changes;
        for (auto const& [key, value] : entries)
        {
            changes[key] = value;
        }
        return task::syncWait([&]() -> task::Task<bcos::h256> {
            storage2::MPTNodeReadStorage reader(m_stateRows);
            auto result = co_await mpt::commitTrie(reader, mpt::emptyRootHash(), changes);
            for (auto const& [hash, rlp] : result.newNodes)
            {
                storage::Entry entry;
                entry.set(bcos::bytes(rlp));
                co_await storage2::writeOne(
                    m_stateRows, storage2::mptNodeStateKey(hash), std::move(entry));
            }
            co_return result.root;
        }());
    }

    void buildTrie()
    {
        auto const storageRoot = commitIntoStateRows(
            {{mpt::slotKeyHash(slotA), valueA}, {mpt::slotKeyHash(slotB), valueB}});

        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000;
        account.storageRoot = storageRoot;
        stateRoot = commitIntoStateRows({{mpt::accountKeyHash(address), account.encode()}});
    }

    /// Build a state trie whose account leaf has a non-zero nonce/balance but an EMPTY storage
    /// root — a scenario-A first-touch account (MPTBuilder.h:307-310): pulled into the trie by
    /// a balance change, with its pre-activation storage absent from the storage sub-trie.
    void buildEmptyStorageTrie()
    {
        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000;
        // storageRoot stays default (emptyRootHash()).
        stateRoot = commitIntoStateRows({{mpt::accountKeyHash(address), account.encode()}});
    }

    /// The production wiring shape (AirNodeInitializer): the AnyStorage handle owns its
    /// adapter; only m_stateRows (the Initializer-owned backend stand-in) is borrowed.
    void wireReader() { nodeService->setMPTNodeReader(storage2::makeMPTNodeReader(m_stateRows)); }

    /// The production wiring shape (AirNodeInitializer): a provider that hands back an
    /// owning AnyStorage over the latest-state plane, forked per request.
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
            executor_v1::StateKey{std::string(bcos::ledger::SYS_DIRECTORY::USER_APPS) +
                                      address.hex(),
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
    StateRowStorage m_stateRows;
    LatestStateStorage m_latestState;
    bcos::Address address{std::string("0x00000000000000000000000000000000000000ab")};
    h256 slotA{1U};
    h256 slotB{2U};
    bytes valueA{0x2a};              // RLP(42)
    bytes valueB{0x82, 0x13, 0x37};  // RLP(0x1337)
    h256 stateRoot;
};

BOOST_FIXTURE_TEST_SUITE(EthGetStorageAtTest, EthGetStorageAtFixture)

/// The 32-byte padded rendering of a u256 (eth_getStorageAt's result shape).
static std::string paddedHex(bcos::u256 value)
{
    return "0x" + toHex(bcos::h256{value}.ref());
}

// Latest state, provider wired: the flat KV read must come from the forked view.
BOOST_AUTO_TEST_CASE(LatestStateFromForkedView)
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

// Historical state, MPT wired: the slot must come from the block's committed state root.
BOOST_AUTO_TEST_CASE(HistoricalSlotFromMPTRoot)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));

    auto resp2 = getStorageAt(address.hexPrefixed(), "0x2", "0x1");
    BOOST_TEST(!resp2.isMember("error"));
    BOOST_REQUIRE(resp2.isMember("result"));
    BOOST_TEST(resp2["result"].asString() == paddedHex(0x1337));
}

// Historical state, scenario B (feature_l2_ethereum_compat): the storage tries are complete,
// so a slot absent from the trie provably reads zero.
BOOST_AUTO_TEST_CASE(HistoricalAbsentSlotScenarioBReadsZero)
{
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    m_ledger->setFeatures(std::move(features));

    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x5", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(0));
}

// Historical state, scenario A (default — MPT activated mid-chain): a slot absent from the
// incomplete storage trie is dormant, and its flat KV value is authoritative (it never
// changed after activation). The read falls back to the flat state, exactly like getProof.
BOOST_AUTO_TEST_CASE(HistoricalAbsentSlotScenarioAFallsBackToFlat)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    // slot 5 is absent from the trie; give it a non-zero flat value (dormant slot).
    h256 slotC{5U};
    bcos::bytes value32(32, 0);
    value32.back() = 0x2a;
    storage::Entry entry;
    entry.set(bcos::bytes(value32));
    m_ledger->setStorageAt(address.hex(),
        std::string{reinterpret_cast<char const*>(slotC.ref().data()), h256::SIZE},
        std::move(entry));

    auto resp = getStorageAt(address.hexPrefixed(), "0x5", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Historical state, scenario A: an account absent from the incomplete trie may be dormant
// (real non-zero state) rather than non-existent — indistinguishable at this root, so every
// state-read endpoint errors explicitly, exactly like getProof's AccountNotInMPT.
BOOST_AUTO_TEST_CASE(HistoricalDormantAccountScenarioAErrors)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    std::string const dormant = "0x00000000000000000000000000000000000000cc";
    for (auto const& [method, resp] : {std::make_pair("eth_getStorageAt", getStorageAt(dormant, "0x1", "0x1")),
             std::make_pair("eth_getBalance", getBalance(dormant, "0x1")),
             std::make_pair("eth_getTransactionCount", getTransactionCount(dormant, "0x1")),
             std::make_pair("eth_getCode", getCode(dormant, "0x1"))})
    {
        BOOST_REQUIRE_MESSAGE(resp.isMember("error"), method);
        BOOST_CHECK_MESSAGE(resp["error"]["code"].asInt() == -32004, method);
        BOOST_CHECK_MESSAGE(resp["error"]["message"].asString().find("Account not in trie") !=
                                std::string::npos,
            method);
    }
}

// Historical state, scenario B: a genuinely absent account provably has no state → zero.
BOOST_AUTO_TEST_CASE(HistoricalDormantAccountScenarioBReadsZero)
{
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    m_ledger->setFeatures(std::move(features));

    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    std::string const absent = "0x00000000000000000000000000000000000000cc";
    auto resp = getStorageAt(absent, "0x1", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(0));
}

// Historical state, scenario A: an account PRESENT in the trie but with an EMPTY storage
// root (first touch wrote only nonce/balance/code — MPTBuilder.h:307-310) has no storage in
// the sub-trie at all. A slot query must fall back to the flat KV (its pre-activation
// storage), not report a silent zero.
BOOST_AUTO_TEST_CASE(HistoricalEmptyStorageRootScenarioAFallsBackToFlat)
{
    buildEmptyStorageTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    // The pre-activation storage lives in the flat KV.
    bcos::bytes value32(32, 0);
    value32.back() = 0x2a;
    storage::Entry entry;
    entry.set(bcos::bytes(value32));
    m_ledger->setStorageAt(address.hex(),
        std::string{reinterpret_cast<char const*>(slotA.ref().data()), h256::SIZE},
        std::move(entry));

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Historical state, scenario B: the same empty-storage-root account reads zero (complete
// trie — the account genuinely has no storage).
BOOST_AUTO_TEST_CASE(HistoricalEmptyStorageRootScenarioBReadsZero)
{
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    m_ledger->setFeatures(std::move(features));

    buildEmptyStorageTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(0));
}

// Historical state, no MPT node reader wired: -32603, never a silent latest answer.
BOOST_AUTO_TEST_CASE(HistoricalWithoutMptReaderReturns32603)
{
    buildTrie();
    // no wireReader()
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(resp["error"]["message"].asString().find("MPT not enabled") != std::string::npos);
}

// Historical state, root absent from MPT node rows (block predates MPT activation):
// -32004 with the missing-root message.
BOOST_AUTO_TEST_CASE(HistoricalMissingRootReturns32004)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(h256{0x1234U});

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(resp["error"]["message"].asString().find("not in MPT node storage") !=
                std::string::npos);
}

// Historical state, empty root: like getProof (generateProof's BlockNotCommitted), the empty
// trie root has no node row, so it is treated as "root not committed" — an explicit error,
// never a silent zero.
BOOST_AUTO_TEST_CASE(HistoricalEmptyRootReturns32004)
{
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(mpt::emptyRootHash());

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "0x1");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(resp["error"]["message"].asString().find("not in MPT node storage") !=
                std::string::npos);
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
    BOOST_CHECK(resp["error"]["message"].asString().find("storage position") !=
                std::string::npos);
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
// committed historical block served from the MPT, never the head). latest = 19, depth 1 →
// safe = block 18.
BOOST_AUTO_TEST_CASE(SafeTagResolvesToHistoricalMpt)
{
    nodeService->setSafeBlockDepth(1);
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[18]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "safe");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// blockTag semantics: with a configured finalizedBlockDepth, "finalized" resolves to
// latest - depth (a committed historical block served from the MPT). latest = 19, depth 2 →
// finalized = block 17.
BOOST_AUTO_TEST_CASE(FinalizedTagResolvesToHistoricalMpt)
{
    nodeService->setFinalizedBlockDepth(2);
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[17]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "finalized");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// The safe/finalized depths are configurable: safeBlockDepth = 2 → "safe" = block 17.
BOOST_AUTO_TEST_CASE(ConfigurableSafeDepth)
{
    nodeService->setSafeBlockDepth(2);
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[17]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getStorageAt(address.hexPrefixed(), "0x1", "safe");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == paddedHex(42));
}

// Historical getBalance: the balance comes from the block's committed MPT root (1000).
BOOST_AUTO_TEST_CASE(HistoricalBalanceFromMPT)
{
    buildTrie();  // account.balance = 1000, nonce = 7
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getBalance(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toQuantity(1000));
}

// Historical getTransactionCount: the nonce comes from the block's committed MPT root (7).
BOOST_AUTO_TEST_CASE(HistoricalNonceFromMPT)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getTransactionCount(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toQuantity(7));
}

// Historical getCode: the code comes from the account leaf's codeHash, resolved through the
// content-addressed s_code_binary store at that root.
BOOST_AUTO_TEST_CASE(HistoricalCodeFromMPT)
{
    bcos::bytes code{0x60, 0x00, 0x60, 0x00};
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 codeHash;
    bcos::crypto::hasher::hash(hasher, bcos::ref(code), codeHash);

    mpt::Account account;
    account.codeHash = codeHash;
    stateRoot = commitIntoStateRows({{mpt::accountKeyHash(address), account.encode()}});

    // s_code_binary row (content-addressed, readable at any block height).
    auto const stateStorage = m_ledger->getStateStorage();
    storage::Entry codeEntry;
    codeEntry.set(bcos::bytes(code));
    task::syncWait(storage2::writeOne(*stateStorage,
        executor_v1::StateKey{bcos::ledger::SYS_CODE_BINARY, codeHash.toRawString()},
        std::move(codeEntry)));

    wireReader();
    m_ledger->ledgerData()[1]->blockHeader()->setStateRoot(stateRoot);

    auto resp = getCode(address.hexPrefixed(), "0x1");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_TEST(resp["result"].asString() == toHexStringWithPrefix(code));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
