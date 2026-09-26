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
 * @file EthGetProofRpcTest.cpp
 * @brief eth_getProof EIP-1186 JSON-RPC endpoint tests (M8.3, spec §5.9 / §6.2)
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/SystemConfigs.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace bcos::test
{
namespace mpt = bcos::ledger::mpt;

class EthGetProofFixture : public RPCFixture
{
public:
    using MPTNodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

    EthGetProofFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
        // These cases assert scenario-B semantics (the Ethereum lane, executor_version >= 2:
        // complete storage tries, exclusion = provable zero). The scenario-A SlotNotInMPT
        // behavior is covered by EthGetProofSlotNotInMPTTest.cpp. The endpoint resolves the
        // lane per block from the executor_version SYS_CONFIG entry
        // (LedgerInterface::fetchExecutorVersionAt).
        m_ledger->setSystemConfig(
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(bcos::ledger::ETHEREUM_EXECUTOR_VERSION));
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

    /// Build one account owning a two-slot storage trie into m_mptNodes and stamp the resulting
    /// state root onto the latest fake block header ("latest" resolves to it). Reader wiring is a
    /// separate step so the unset-reader case can reuse the same trie setup.
    void buildTrie()
    {
        auto const storageRoot = commitInto(
            m_mptNodes, {{mpt::slotKeyHash(slotA), valueA}, {mpt::slotKeyHash(slotB), valueB}});

        mpt::Account account;
        account.nonce = 7;
        account.balance = 1000;
        account.storageRoot = storageRoot;
        stateRoot = commitInto(m_mptNodes, {{mpt::accountKeyHash(address), account.encode()}});

        m_ledger->ledgerData().back()->blockHeader()->setStateRoot(stateRoot);
    }

    /// Inject the type-erased MPT node reader; the AnyStorage view is non-owning, m_mptNodes
    /// (a fixture member) plays the Initializer's ownership role and outlives the NodeService.
    void wireReader()
    {
        nodeService->setMPTNodeReader(
            std::make_shared<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>>(m_mptNodes));
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

    /// Rebuild the binary EIP1186Proof from the endpoint's JSON: hex fields back to bytes, and
    /// each slot "value" (a QUANTITY per EIP-1186) back to the trie's RLP leaf encoding via
    /// encodeStorageValue — verifyProof compares against the raw leaf bytes.
    static mpt::EIP1186Proof proofFromJson(Json::Value const& result)
    {
        mpt::EIP1186Proof out;
        out.address = Address(result["address"].asString(), Address::FromHex);
        out.balance = fromBigQuantity(result["balance"].asString());
        out.nonce = fromBigQuantity(result["nonce"].asString());
        out.codeHash = h256(result["codeHash"].asString(), h256::FromHex);
        out.storageHash = h256(result["storageHash"].asString(), h256::FromHex);
        for (auto const& node : result["accountProof"])
        {
            out.accountProof.push_back(fromHexWithPrefix(node.asString()));
        }
        for (auto const& entryJson : result["storageProof"])
        {
            mpt::StorageProof entry;
            entry.key = h256(entryJson["key"].asString(), h256::FromHex);
            auto raw = quantityToBytes(entryJson["value"].asString());
            entry.value = mpt::encodeStorageValue(bcos::ref(raw));
            for (auto const& node : entryJson["proof"])
            {
                entry.proof.push_back(fromHexWithPrefix(node.asString()));
            }
            out.storageProof.push_back(std::move(entry));
        }
        return out;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;

    MPTNodeStorage m_mptNodes;
    bcos::Address address{std::string("0x00000000000000000000000000000000000000ab")};
    bcos::Address dormant{std::string("0x00000000000000000000000000000000000000cd")};
    h256 slotA{1U};
    h256 slotB{2U};
    h256 slotMissing{3U};
    bytes valueA{0x2a};              // RLP(42): single byte < 0x80
    bytes valueB{0x82, 0x13, 0x37};  // RLP(0x1337): 2-byte string
    h256 stateRoot;
};

BOOST_FIXTURE_TEST_SUITE(EthGetProofRpcTest, EthGetProofFixture)

// eth_getProof must be registered in EndpointsMapping (method-name lookup succeeds).
BOOST_AUTO_TEST_CASE(MethodRegistered)
{
    EndpointsMapping const mapping;
    BOOST_CHECK(mapping.findHandler("eth_getProof").has_value());
    BOOST_CHECK(!mapping.findHandler("eth_getProofX").has_value());
}

// Happy path: full EIP-1186 shape over a real one-account/two-slot trie, plus a round trip — the
// JSON-serialized proof fed back through verifyProof must still verify, proving the RPC layer
// does not corrupt the proof bytes.
BOOST_AUTO_TEST_CASE(HappyPathShapeAndRoundTrip)
{
    buildTrie();
    wireReader();

    auto resp = getProof(address.hexPrefixed(),
        {slotA.hexPrefixed(), slotB.hexPrefixed(), slotMissing.hexPrefixed()}, "latest");
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];

    BOOST_TEST(result["address"].asString() == address.hexPrefixed());
    BOOST_TEST(result["balance"].asString() == "0x3e8");
    BOOST_TEST(result["nonce"].asString() == "0x7");
    BOOST_TEST(result["codeHash"].asString() == mpt::emptyCodeHash().hexPrefixed());
    BOOST_REQUIRE(result["accountProof"].isArray());
    BOOST_TEST(result["accountProof"].size() >= 1U);

    BOOST_REQUIRE(result["storageProof"].isArray());
    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 3U);
    auto const& proofA = result["storageProof"][0U];
    auto const& proofB = result["storageProof"][1U];
    auto const& proofMissing = result["storageProof"][2U];
    BOOST_TEST(proofA["key"].asString() == slotA.hexPrefixed());
    // The trie leaf stores RLP(0x2a); EIP-1186 "value" is the decoded QUANTITY.
    BOOST_TEST(proofA["value"].asString() == "0x2a");
    BOOST_TEST(proofA["proof"].size() >= 1U);
    BOOST_TEST(proofB["value"].asString() == "0x1337");
    // Absent slot: zero value with a non-empty exclusion proof.
    BOOST_TEST(proofMissing["value"].asString() == "0x0");
    BOOST_TEST(proofMissing["proof"].size() >= 1U);

    auto const reconstructed = proofFromJson(result);
    auto const verify = mpt::verifyProof(stateRoot, reconstructed);
    BOOST_TEST(verify.accountValid);
    BOOST_REQUIRE_EQUAL(verify.storageValid.size(), 3U);
    BOOST_TEST(verify.storageValid[0]);
    BOOST_TEST(verify.storageValid[1]);
    BOOST_TEST(verify.storageValid[2]);
}

// Dormant account (present state root, address not in the trie) -> -32004 "not in trie".
BOOST_AUTO_TEST_CASE(DormantAccountReturns32004)
{
    buildTrie();
    wireReader();

    auto resp = getProof(dormant.hexPrefixed(), {}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(resp["error"]["message"].asString().find("not in trie") != std::string::npos);
}

// Header stateRoot absent from the MPT node storage -> -32004 "not in MPT node storage".
BOOST_AUTO_TEST_CASE(UnknownStateRootReturns32004)
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

// No MPT node reader wired (production: a tars-built NodeService) -> -32603 "MPT not enabled".
BOOST_AUTO_TEST_CASE(ReaderUnsetReturns32603)
{
    buildTrie();  // trie + stateRoot exist, but the reader stays unset

    auto resp = getProof(address.hexPrefixed(), {slotA.hexPrefixed()}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(resp["error"]["message"].asString().find("MPT not enabled") != std::string::npos);
}

// F4 regression: a 66-char "0x…" third parameter is a block hash (op-node passes it). A
// malformed hex hash is a client error ("Invalid block hash"), not the misleading
// "Block not found" the earlier catch-all produced for both malformed hex and storage faults.
BOOST_AUTO_TEST_CASE(MalformedBlockHashReturnsInvalidBlockHash)
{
    buildTrie();
    wireReader();

    // 66 chars ("0x" + 64) so the hash branch is taken, but the payload is not valid hex — the
    // decode fails before any ledger lookup, so the fixture needs no hash->number support.
    std::string malformed = "0x" + std::string(64, 'z');
    auto resp = getProof(address.hexPrefixed(), {}, malformed);
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);  // InvalidParams
    BOOST_CHECK(
        resp["error"]["message"].asString().find("Invalid block hash") != std::string::npos);
}

// A valid 66-char hash (op-node passes the block hash for output-root computation) resolves to
// the same proof as the equivalent block number: the hash branch lands on the same header.
BOOST_AUTO_TEST_CASE(HashFormReturnsSameProofAsNumber)
{
    // Capture the latest block hash BEFORE buildTrie — setStateRoot clears dataHash, so
    // hash() would throw afterwards. The fixture's FakeLedger maps this hash to its number.
    auto const latestHash = m_ledger->ledgerData().back()->blockHeader()->hash();

    buildTrie();
    wireReader();

    auto respByHash = getProof(address.hexPrefixed(), {slotA.hexPrefixed()}, latestHash.hexPrefixed());
    BOOST_REQUIRE(!respByHash.isMember("error"));
    BOOST_REQUIRE(respByHash.isMember("result"));

    auto respByNumber = getProof(address.hexPrefixed(), {slotA.hexPrefixed()}, "latest");
    BOOST_REQUIRE(!respByNumber.isMember("error"));
    BOOST_REQUIRE(respByNumber.isMember("result"));

    // The two paths must produce identical account and slot proofs (same stateRoot).
    BOOST_CHECK_EQUAL(respByHash["result"]["balance"].asString(),
        respByNumber["result"]["balance"].asString());
    BOOST_CHECK_EQUAL(respByHash["result"]["nonce"].asString(), respByNumber["result"]["nonce"].asString());
    BOOST_CHECK_EQUAL(respByHash["result"]["storageHash"].asString(),
        respByNumber["result"]["storageHash"].asString());
    BOOST_REQUIRE(respByHash["result"]["storageProof"].isArray());
    BOOST_REQUIRE(respByNumber["result"]["storageProof"].isArray());
    BOOST_CHECK_EQUAL(respByHash["result"]["storageProof"][0U]["value"].asString(),
        respByNumber["result"]["storageProof"][0U]["value"].asString());
}

// An unknown 66-char hash answers InvalidParams "Block not found" — the GetStorageError without
// a chained STDError is a not-found, not a storage fault (which would surface as an internal
// error instead).
BOOST_AUTO_TEST_CASE(UnknownHashReturnsBlockNotFound)
{
    buildTrie();
    wireReader();

    // 66-char valid hex that maps to no block in the fake ledger.
    std::string unknownHash = "0x" + std::string(64, '0');
    auto resp = getProof(address.hexPrefixed(), {}, unknownHash);
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);  // InvalidParams
    BOOST_CHECK(resp["error"]["message"].asString().find("Block not found") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
