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
 * @file EthGetProofSlotNotInMPTTest.cpp
 * @brief eth_getProof SlotNotInMPT JSON shape under scenario A (spec §5.9 / §4.4)
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

// FakeLedger's default (empty) feature set leaves feature_l2_ethereum_compat OFF, so every case
// here runs under scenario A unless it opts in to scenario B explicitly.
class EthGetProofSlotNotInMPTFixture : public RPCFixture
{
public:
    using MPTNodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

    EthGetProofSlotNotInMPTFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
        buildTrie();
        nodeService->setMPTNodeReader(
            std::make_shared<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>>(m_mptNodes));
    }

    /// Commit @p entries into a fresh trie in @p storage and flush the produced nodes, returning
    /// the root. commitTrie only computes; persistence is the caller's job (HashBuilder.h).
    static bcos::h256 commitInto(
        MPTNodeStorage& storage, std::map<bcos::h256, bcos::bytes> const& entries)
    {
        std::map<bcos::h256, std::optional<bcos::bytes>> changes;
        for (auto const& [key, value] : entries)
        {
            changes[key] = value;
        }
        auto result = task::syncWait(mpt::commitTrie(storage, mpt::emptyRootHash(), changes));
        task::syncWait(mpt::flushTrieNodes(storage, result.newNodes));
        return result.root;
    }

    /// One account whose storage trie holds ONLY slotHot; slotCold stays out of the trie —
    /// under scenario A that makes it a cold slot, not a provable zero.
    void buildTrie()
    {
        auto const storageRoot = commitInto(m_mptNodes, {{mpt::slotKeyHash(slotHot), hotValue}});

        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000;
        account.storageRoot = storageRoot;
        stateRoot = commitInto(m_mptNodes, {{mpt::accountKeyHash(address), account.encode()}});

        m_ledger->ledgerData().back()->blockHeader()->setStateRoot(stateRoot);
    }

    void enableL2Mode()
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
        m_ledger->setFeatures(features);
    }

    /// A JSON quantity ("0x2a") back to trimmed big-endian bytes; "0x0" -> empty.
    static bytes quantityToBytes(std::string const& quantity)
    {
        std::string hex = quantity;
        if (hex.starts_with("0x") || hex.starts_with("0X"))
        {
            hex = hex.substr(2);
        }
        if (hex.empty() || hex == "0")
        {
            return {};
        }
        if (hex.size() % 2 != 0)
        {
            hex.insert(0, "0");
        }
        return fromHex(hex);
    }

    /// Store a 32-byte big-endian flat value for @p slot the way the endpoint reads it back:
    /// ledger::getStorageAt with the lowercase unprefixed address and the raw 32-byte key.
    void setFlatValue(h256 const& slot, h256 const& value)
    {
        storage::Entry entry;
        entry.set(value.toRawString());
        m_ledger->setStorageAt(address.hex(), slot.toRawString(), std::move(entry));
    }

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

    Json::Value getProof(std::vector<std::string> const& keys)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_getProof";
        Json::Value params(Json::arrayValue);
        params.append(address.hexPrefixed());
        Json::Value keysJson(Json::arrayValue);
        for (auto const& key : keys)
        {
            keysJson.append(key);
        }
        params.append(keysJson);
        params.append("latest");
        req["params"] = params;
        return request(printJson(req));
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;

    MPTNodeStorage m_mptNodes;
    bcos::Address address{std::string("0x00000000000000000000000000000000000000ab")};
    h256 slotHot{1U};
    h256 slotCold{3U};
    bytes hotValue{0x2a};  // RLP(42): single byte < 0x80
    h256 stateRoot;
};

BOOST_FIXTURE_TEST_SUITE(EthGetProofSlotNotInMPTTest, EthGetProofSlotNotInMPTFixture)

// Scenario A, cold slot with a non-zero flat value: "value" is the authoritative flat-KV truth,
// "proof" the empty array, "inMPT" false — NOT a value-0 exclusion proof, which would lie about
// a slot whose flat value is non-zero. The hot slot proves normally alongside it.
BOOST_AUTO_TEST_CASE(ColdSlotFlatValueNoProof)
{
    setFlatValue(slotCold, h256{0x1337U});  // 32-byte padded: exercises quantity trimming too

    auto resp = getProof({slotHot.hexPrefixed(), slotCold.hexPrefixed()});
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 2U);

    auto const& hot = result["storageProof"][0U];
    BOOST_TEST(hot["key"].asString() == slotHot.hexPrefixed());
    BOOST_TEST(hot["value"].asString() == "0x2a");
    BOOST_TEST(hot["proof"].size() >= 1U);
    BOOST_TEST(hot["inMPT"].asBool());

    auto const& cold = result["storageProof"][1U];
    BOOST_TEST(cold["key"].asString() == slotCold.hexPrefixed());
    BOOST_TEST(cold["value"].asString() == "0x1337");
    BOOST_REQUIRE(cold["proof"].isArray());
    BOOST_TEST(cold["proof"].size() == 0U);
    BOOST_TEST(!cold["inMPT"].asBool());
}

// Scenario A, cold slot with NO flat entry: the authoritative value is zero — still no proof.
BOOST_AUTO_TEST_CASE(ColdSlotWithoutFlatValueReadsZero)
{
    auto resp = getProof({slotCold.hexPrefixed()});
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& cold = resp["result"]["storageProof"][0U];
    BOOST_TEST(cold["value"].asString() == "0x0");
    BOOST_TEST(cold["proof"].size() == 0U);
    BOOST_TEST(!cold["inMPT"].asBool());
}

// Scenario B (feature_l2_ethereum_compat): the same absent slot yields the unchanged EIP-1186
// exclusion proof — value 0x0, non-empty proof, inMPT true — because the complete trie makes
// the exclusion a provable zero. The flat value must NOT leak into the response.
BOOST_AUTO_TEST_CASE(ScenarioBKeepsExclusionProof)
{
    enableL2Mode();
    setFlatValue(slotCold, h256{0x1337U});  // present in flat KV, must be ignored

    auto resp = getProof({slotCold.hexPrefixed()});
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& cold = resp["result"]["storageProof"][0U];
    BOOST_TEST(cold["value"].asString() == "0x0");
    BOOST_TEST(cold["proof"].size() >= 1U);
    BOOST_TEST(cold["inMPT"].asBool());
}

// The scenario-A response round-trips into the verifier contract: the hot slot verifies, the
// cold slot is Unverifiable (not Verified, not a response-level failure).
BOOST_AUTO_TEST_CASE(ColdSlotRoundTripUnverifiable)
{
    setFlatValue(slotCold, h256{0x1337U});

    auto resp = getProof({slotHot.hexPrefixed(), slotCold.hexPrefixed()});
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];

    mpt::EIP1186Proof reconstructed;
    reconstructed.address = Address(result["address"].asString(), Address::FromHex);
    reconstructed.balance = fromBigQuantity(result["balance"].asString());
    reconstructed.nonce = fromBigQuantity(result["nonce"].asString());
    reconstructed.codeHash = h256(result["codeHash"].asString(), h256::FromHex);
    reconstructed.storageHash = h256(result["storageHash"].asString(), h256::FromHex);
    for (auto const& node : result["accountProof"])
    {
        reconstructed.accountProof.push_back(fromHexWithPrefix(node.asString()));
    }
    for (auto const& entryJson : result["storageProof"])
    {
        mpt::StorageProof entry;
        entry.key = h256(entryJson["key"].asString(), h256::FromHex);
        entry.inMPT = entryJson["inMPT"].asBool();
        auto raw = quantityToBytes(entryJson["value"].asString());
        // In-trie values compare against the RLP leaf encoding; a flat-KV assertion
        // (inMPT=false) is carried as-is — the verifier ignores it either way.
        entry.value = entry.inMPT ? mpt::encodeStorageValue(bcos::ref(raw)) : raw;
        for (auto const& node : entryJson["proof"])
        {
            entry.proof.push_back(fromHexWithPrefix(node.asString()));
        }
        reconstructed.storageProof.push_back(std::move(entry));
    }

    auto const verify = mpt::verifyProof(stateRoot, reconstructed);
    BOOST_TEST(verify.accountValid);
    BOOST_REQUIRE_EQUAL(verify.storageStatus.size(), 2U);
    BOOST_TEST(verify.storageValid[0]);
    BOOST_CHECK(verify.storageStatus[0] == mpt::SlotProofStatus::Verified);
    BOOST_TEST(!verify.storageValid[1]);
    BOOST_CHECK(verify.storageStatus[1] == mpt::SlotProofStatus::Unverifiable);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
