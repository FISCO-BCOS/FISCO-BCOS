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
 * @brief eth_getProof over the PRODUCTION reader shape (M8.3 wiring): trie nodes stored as
 *        ordinary StateKey rows ("/mpt/" plane), read back through
 *        makeMPTNodeReader(stateRowStorage) — the same adapter chain the AIR initializer
 *        injects into NodeService — instead of EthGetProofRpcTest's direct h256->bytes map.
 * @file EthGetProofReaderWiringTest.cpp
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-storage/MPTNodeReadStorage.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <optional>
#include <string>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

class EthGetProofWiringFixture : public RPCFixture
{
public:
    /// The state-row plane standing in for the committed backend
    /// (GlobalStateStorage::latestBackend() in production): StateKey -> Entry, no MPT types.
    using StateRowStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

    EthGetProofWiringFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    /// Commit @p entries into the trie whose nodes live as "/mpt/" STATE ROWS: parent nodes
    /// are read through the very adapter under test, and the produced nodes are flushed back
    /// through the ordinary state write path (Entry keyed mptNodeStateKey) — write side and
    /// read side meet only at the row layout, exactly like scheduler-commit vs rpc-read.
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

        m_ledger->ledgerData().back()->blockHeader()->setStateRoot(stateRoot);
    }

    /// The production wiring shape (AirNodeInitializer): the AnyStorage handle owns its
    /// adapter; only m_stateRows (the Initializer-owned backend stand-in) is borrowed.
    void wireReader() { nodeService->setMPTNodeReader(storage2::makeMPTNodeReader(m_stateRows)); }

    Json::Value request(std::string const& req)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            req, [&promise](bcos::bytes resp, boost::beast::http::status) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Json::Value getProof(
        std::string const& addressHex, std::vector<std::string> const& keys, std::string const& tag)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getProof";
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        Json::Value keysJson(Json::arrayValue);
        for (auto const& key : keys)
        {
            keysJson.append(key);
        }
        params.append(keysJson);
        params.append(tag);
        req["params"] = params;
        return request(printJson(req));
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;

    StateRowStorage m_stateRows;
    bcos::Address address{std::string("0x00000000000000000000000000000000000000ab")};
    h256 slotA{1U};
    h256 slotB{2U};
    bytes valueA{0x2a};              // RLP(42)
    bytes valueB{0x82, 0x13, 0x37};  // RLP(0x1337)
    h256 stateRoot;
};

BOOST_FIXTURE_TEST_SUITE(EthGetProofReaderWiringTest, EthGetProofWiringFixture)

// Happy path through the full production reader chain: state rows -> MPTNodeReadStorage ->
// AnyStorage -> NodeService -> generateProof.
BOOST_AUTO_TEST_CASE(WiredReaderServesProofFromStateRows)
{
    buildTrie();
    wireReader();

    auto resp =
        getProof(address.hexPrefixed(), {slotA.hexPrefixed(), slotB.hexPrefixed()}, "latest");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_TEST(result["address"].asString() == address.hexPrefixed());
    BOOST_TEST(result["balance"].asString() == "0x3e8");
    BOOST_TEST(result["nonce"].asString() == "0x7");
    BOOST_REQUIRE(result["accountProof"].isArray());
    BOOST_TEST(result["accountProof"].size() >= 1U);
    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 2U);
    BOOST_TEST(result["storageProof"][0U]["value"].asString() == "0x2a");
    BOOST_TEST(result["storageProof"][1U]["value"].asString() == "0x1337");
}

// Negative control for the wiring itself: the SAME trie sits in the state rows, but with no
// setMPTNodeReader call the endpoint must still answer -32603 — proving the happy path above
// passes because of the injected reader, not some other route to the rows.
BOOST_AUTO_TEST_CASE(UnwiredReaderStillReturns32603)
{
    buildTrie();
    // no wireReader()

    auto resp = getProof(address.hexPrefixed(), {}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(resp["error"]["message"].asString().find("MPT not enabled") != std::string::npos);
}

// A header stateRoot with no node row behind it must surface as -32004 through the adapter:
// the adapter's nullopt-on-miss is what generateProof turns into BlockNotCommitted.
BOOST_AUTO_TEST_CASE(MissingRootThroughAdapterReturns32004)
{
    buildTrie();
    wireReader();
    m_ledger->ledgerData().back()->blockHeader()->setStateRoot(h256{0x1234U});

    auto resp = getProof(address.hexPrefixed(), {}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("not in MPT node storage") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
