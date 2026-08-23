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
 * @file EthGetProofFlatStateTest.cpp
 * @brief eth_getProof on the OP-stack flat-state path: the trie is REBUILT from the committed
 *        flat plane (no persisted MPT nodes) and gated on the requested block's stateRoot
 *        (ledger::mpt::generateProofFromFlat). Seeds a flat /apps/ plane, stamps the derived
 *        root onto a baseFee-carrying header (the OP-path discriminator), and checks both the
 *        struct-level proof (via the independent verifyProof walker) and the JSON endpoint.
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/FlatProof.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rpc/Rpc.h>
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

/// Wrap a state storage in an owning AnyStorage handle — the production
/// forkCommittedStateView shape, inlined because the test target does not link libinitializer.
template <class Storage>
std::shared_ptr<rpc::NodeService::StateStorage> makeOwningFlatStorage(Storage& storage)
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

class EthGetProofFlatStateFixture : public RPCFixture
{
public:
    /// The committed flat plane stand-in: ordered + logical deletion, the production
    /// GlobalStateMutableStorage shape (range values arrive as the tombstone-capable variant).
    using FlatStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::LOGICAL_DELETION>;

    EthGetProofFlatStateFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
        seedFlatState();
    }

    /// Shadows RPCFixture's RPCInterface::Ptr rpc with the concrete type — web3JsonRpc() is a
    /// Rpc member (same shadowing pattern as Web3RpcTest / EthGetStorageAtTest fixtures).
    bcos::rpc::Rpc::Ptr rpc;
    bcos::rpc::Web3JsonRpcImpl::Ptr web3JsonRpc;

    // ---- deterministic addresses / values ----
    bcos::Address const targetAddr{
        std::string_view{"1111111111111111111111111111110000000001"}, bcos::Address::FromHex};
    bcos::Address const otherAddr{
        std::string_view{"1111111111111111111111111111110000000002"}, bcos::Address::FromHex};
    bcos::Address const absentAddr{
        std::string_view{"1111111111111111111111111111110000000003"}, bcos::Address::FromHex};
    bcos::h256 const slotA{"000000000000000000000000000000000000000000000000000000000000000a"};
    bcos::h256 const slotB{"000000000000000000000000000000000000000000000000000000000000000b"};
    bcos::h256 const zeroSlot{"000000000000000000000000000000000000000000000000000000000000000c"};
    bcos::h256 const tombstonedSlot{
        "000000000000000000000000000000000000000000000000000000000000000d"};
    bcos::h256 const neverSlot{"000000000000000000000000000000000000000000000000000000000000000e"};
    bcos::h256 const targetCodeHash{
        "2222222222222222222222222222222222222222222222222222222222222222"};
    /// 32-byte slot values; valueA trims to 0x64 (100) in its trie leaf, valueB keeps 32 bytes.
    bcos::bytes const valueA = bcos::bytes(31, 0x00) + bcos::bytes{0x64};
    bcos::bytes const valueB = [] {
        bcos::bytes v(32, 0x77);
        v[0] = 0x11;
        return v;
    }();

    void writeRow(std::string_view table, std::string_view field, bcos::bytes value)
    {
        storage::Entry entry;
        entry.set(std::move(value));
        task::syncWait(storage2::writeOne(m_flat,
            executor_v1::StateKey{executor_v1::StateKeyView{table, field}}, std::move(entry)));
    }

    void writeRow(std::string_view table, std::string_view field, std::string_view value)
    {
        writeRow(table, field, bcos::bytes(value.begin(), value.end()));
    }

    /// Raw 32-byte hash as the big-endian byte string the executor stores.
    static bcos::bytes toBytes(bcos::h256 const& hash)
    {
        return bcos::bytes(hash.ref().data(), hash.ref().data() + bcos::h256::SIZE);
    }

    void removeRow(std::string_view table, std::string_view field)
    {
        task::syncWait(storage2::removeOne(
            m_flat, executor_v1::StateKey{executor_v1::StateKeyView{table, field}}));
    }

    /// Raw 32-byte slot key as the row-field string the executor writes.
    static std::string_view slotField(bcos::h256 const& slot)
    {
        return {reinterpret_cast<char const*>(slot.ref().data()), bcos::h256::SIZE};
    }

    static std::string tableName(bcos::Address const& addr)
    {
        return std::string{bcos::ledger::SYS_DIRECTORY::USER_APPS} + addr.hex();
    }

    /// The account trie the flat rows are expected to rebuild to — assembled with the same
    /// leaf/key primitives the seal-time stateRootOf uses (Account::encode + slotKeyHash +
    /// encodeStorageValue + computeTrieRoot), so the header's root and the fixture agree.
    [[nodiscard]] bcos::h256 expectedRoot() const
    {
        std::map<bcos::h256, bcos::bytes> targetStorage;
        targetStorage[mpt::slotKeyHash(slotA)] =
            mpt::encodeStorageValue(bcos::bytesConstRef(valueA.data(), valueA.size()));
        targetStorage[mpt::slotKeyHash(slotB)] =
            mpt::encodeStorageValue(bcos::bytesConstRef(valueB.data(), valueB.size()));
        auto const targetStorageRoot = mpt::computeTrieRoot(targetStorage).root;

        mpt::Account target;
        target.nonce = 7;
        target.balance = 1000000;
        target.storageRoot = targetStorageRoot;
        target.codeHash = targetCodeHash;

        mpt::Account other;  // balance only: nonce 0, empty storage, keccak("") code
        other.balance = 42;

        return mpt::computeTrieRoot({{mpt::accountKeyHash(targetAddr), target.encode()},
                                        {mpt::accountKeyHash(otherAddr), other.encode()}})
            .root;
    }

    /// A root derived from DIFFERENT state — every byte-level disagreement with the flat plane.
    [[nodiscard]] bcos::h256 divergentRoot() const
    {
        mpt::Account divergent;
        divergent.balance = 43;
        return mpt::computeTrieRoot({{mpt::accountKeyHash(otherAddr), divergent.encode()}}).root;
    }

    /// Seed the flat plane: two /apps/ accounts (registered in SYS_TABLES), Ethereum rows plus
    /// the rows that must be IGNORED (a zero-valued slot, a tombstoned slot, a BCOS extension
    /// field), and a /apps/ non-account table that parseAccountTable rejects.
    void seedFlatState()
    {
        auto const targetTable = tableName(targetAddr);
        auto const otherTable = tableName(otherAddr);

        for (auto const* table : {&targetTable, &otherTable})
        {
            writeRow(bcos::ledger::SYS_TABLES, *table, "1");
        }
        // /apps/ holds non-account tables too (BFS links, authorization); they must be skipped.
        writeRow(bcos::ledger::SYS_TABLES,
            std::string{bcos::ledger::SYS_DIRECTORY::USER_APPS} + "not-an-account-table", "1");

        writeRow(targetTable, mpt::ROW_BALANCE, "1000000");
        writeRow(targetTable, mpt::ROW_NONCE, "7");
        writeRow(targetTable, mpt::ROW_CODE_HASH, toBytes(targetCodeHash));
        writeRow(targetTable, slotField(slotA), valueA);
        writeRow(targetTable, slotField(slotB), valueB);
        // Excluded rows: all-zero slot, tombstoned slot, BCOS extension field.
        writeRow(targetTable, slotField(zeroSlot), bcos::bytes(32, 0));
        writeRow(targetTable, slotField(tombstonedSlot), bcos::bytes(32, 0x55));
        removeRow(targetTable, slotField(tombstonedSlot));
        writeRow(targetTable, "abi", "some-metadata");

        writeRow(otherTable, mpt::ROW_BALANCE, "42");

        stampHeader(expectedRoot());
    }

    /// OP-path header: baseFee present (the endpoint's OP discriminator) + the derived root.
    void stampHeader(bcos::h256 const& root)
    {
        auto header = m_ledger->ledgerData().back()->blockHeader();
        header->setBaseFee(bcos::u256(7));
        header->setStateRoot(root);
    }

    /// The production wiring shape (AirNodeInitializer): a provider handing back an owning
    /// AnyStorage over the committed flat plane. No MPT node reader is wired on purpose —
    /// these cases pin the flat FALLBACK in isolation (a pre-①a chain shape); the
    /// node-first ordering with a reader wired is pinned by EthGetProofOpNodePathTest.
    void wireProvider()
    {
        nodeService->setStateStorageProvider([this]() { return makeOwningFlatStorage(m_flat); });
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

    FlatStorage m_flat;
};

// ---- struct level: the rebuilt proof must survive the INDEPENDENT verifier ----

BOOST_FIXTURE_TEST_SUITE(EthGetProofFlatStateTest, EthGetProofFlatStateFixture)

BOOST_AUTO_TEST_CASE(FlatProofVerifiesAgainstIndependentWalker)
{
    auto const root = expectedRoot();

    std::vector<bcos::h256> slots{slotA, slotB, zeroSlot, tombstonedSlot, neverSlot};
    auto result = task::syncWait(
        mpt::generateProofFromFlat(m_flat, root, targetAddr, std::span<bcos::h256 const>(slots)));
    auto* proof = std::get_if<mpt::EIP1186Proof>(&result);
    BOOST_REQUIRE(proof != nullptr);
    BOOST_CHECK_EQUAL(proof->balance, bcos::u256(1000000));
    BOOST_CHECK_EQUAL(proof->nonce, bcos::u256(7));
    BOOST_CHECK(proof->codeHash == targetCodeHash);
    BOOST_CHECK(!proof->accountProof.empty());

    auto verified = mpt::verifyProof(root, *proof);
    BOOST_CHECK(verified.accountValid);
    BOOST_CHECK_EQUAL(verified.recoveredBalance, bcos::u256(1000000));
    BOOST_REQUIRE_EQUAL(proof->storageProof.size(), slots.size());
    for (auto valid : verified.storageValid)
    {
        BOOST_CHECK(valid);
    }
    // Present slots carry their leaf value; absent/zero/tombstoned slots prove as zero (empty).
    BOOST_CHECK(proof->storageProof[0].value ==
                mpt::encodeStorageValue(bcos::bytesConstRef(valueA.data(), valueA.size())));
    BOOST_CHECK(proof->storageProof[1].value ==
                mpt::encodeStorageValue(bcos::bytesConstRef(valueB.data(), valueB.size())));
    BOOST_CHECK(proof->storageProof[2].value.empty());
    BOOST_CHECK(proof->storageProof[3].value.empty());
    BOOST_CHECK(proof->storageProof[4].value.empty());
}

BOOST_AUTO_TEST_CASE(FlatProofAbsentAccountKeepsGethSemantics)
{
    auto const root = expectedRoot();

    std::vector<bcos::h256> slots{slotA};
    auto result = task::syncWait(
        mpt::generateProofFromFlat(m_flat, root, absentAddr, std::span<bcos::h256 const>(slots)));
    auto* proof = std::get_if<mpt::EIP1186Proof>(&result);
    BOOST_REQUIRE(proof != nullptr);
    // op-geth GetProof on a missing state object: zero fields, zero hashes, exclusion walk.
    BOOST_CHECK_EQUAL(proof->balance, bcos::u256(0));
    BOOST_CHECK_EQUAL(proof->nonce, bcos::u256(0));
    BOOST_CHECK(proof->codeHash == bcos::h256{});
    BOOST_CHECK(proof->storageHash == bcos::h256{});
    BOOST_CHECK(!proof->accountProof.empty());
    BOOST_REQUIRE_EQUAL(proof->storageProof.size(), 1U);
    BOOST_CHECK(proof->storageProof[0].key == slotA);
    BOOST_CHECK(proof->storageProof[0].value.empty());
    BOOST_CHECK(proof->storageProof[0].proof.empty());
    BOOST_CHECK(proof->storageProof[0].inMPT);
}

BOOST_AUTO_TEST_CASE(FlatProofRootMismatchIsTheHonestError)
{
    auto result = task::syncWait(mpt::generateProofFromFlat(
        m_flat, divergentRoot(), targetAddr, std::span<bcos::h256 const>{}));
    BOOST_CHECK(std::holds_alternative<mpt::ProofErrorCode>(result));
    BOOST_CHECK(std::get<mpt::ProofErrorCode>(result) == mpt::ProofErrorCode::RootMismatch);
}

// ---- endpoint level: OP-path wiring, JSON shape, error mapping ----

BOOST_AUTO_TEST_CASE(GetProofServesFlatPathThroughJsonRpc)
{
    wireProvider();

    auto resp =
        getProof("0x" + targetAddr.hex(), {"0x" + slotA.hex(), "0x" + neverSlot.hex()}, "latest");
    BOOST_REQUIRE(!resp.isMember("error"));

    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0xf4240");  // 1000000
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x7");
    BOOST_CHECK_EQUAL(result["codeHash"].asString(), "0x" + targetCodeHash.hex());
    BOOST_CHECK_EQUAL(result["storageHash"].asString().size(), 66U);  // 32-byte hash quantity

    BOOST_CHECK(result["accountProof"].isArray());
    BOOST_CHECK_GT(result["accountProof"].size(), 0U);

    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 2U);
    auto const& present = result["storageProof"][0U];
    BOOST_CHECK_EQUAL(present["key"].asString(), "0x" + slotA.hex());
    BOOST_CHECK_EQUAL(present["value"].asString(), "0x64");  // valueA trims to 100
    BOOST_CHECK_GT(present["proof"].size(), 0U);
    auto const& absent = result["storageProof"][1U];
    BOOST_CHECK_EQUAL(absent["value"].asString(), "0x0");
    BOOST_CHECK_GT(absent["proof"].size(), 0U);  // exclusion proof, not the scenario-A empty shape
}

BOOST_AUTO_TEST_CASE(GetProofFlatPathAbsentAccountJson)
{
    wireProvider();

    auto resp = getProof("0x" + absentAddr.hex(), {"0x" + slotA.hex()}, "latest");
    BOOST_REQUIRE(!resp.isMember("error"));
    auto const& result = resp["result"];
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x0");
    // geth parity: zero hashes (not keccak("") / empty-root) for a missing state object.
    BOOST_CHECK_EQUAL(result["codeHash"].asString(), "0x" + std::string(64, '0'));
    BOOST_CHECK_EQUAL(result["storageHash"].asString(), "0x" + std::string(64, '0'));
    BOOST_CHECK_GT(result["accountProof"].size(), 0U);  // exclusion path nodes
    BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 1U);
    BOOST_CHECK_EQUAL(result["storageProof"][0U]["value"].asString(), "0x0");
    BOOST_CHECK(result["storageProof"][0U]["proof"].empty());
}

BOOST_AUTO_TEST_CASE(GetProofFlatPathStaleRootMapsTo32004)
{
    wireProvider();
    // The header claims a root the flat plane does not equal; no retry can fix a divergent
    // claim, and the flat plane holds exactly one state version.
    stampHeader(divergentRoot());

    auto resp = getProof("0x" + targetAddr.hex(), {}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
    BOOST_CHECK(resp["error"]["message"].asString().find("latest committed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(GetProofFlatPathOlderBlockIsNotServed)
{
    wireProvider();
    // An OP-chain header at an older height (every OP header carries baseFee) whose root is
    // not the latest committed state's root — the flat plane serves no history, and the
    // request must fail honestly rather than prove the latest state against an old root.
    for (auto const& block : m_ledger->ledgerData())
    {
        if (block->blockHeader()->number() == 5)
        {
            block->blockHeader()->setBaseFee(bcos::u256(7));
        }
    }
    auto resp = getProof("0x" + targetAddr.hex(), {}, "0x5");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32004);
}

BOOST_AUTO_TEST_CASE(GetProofFlatPathWithoutProviderIsDeploymentError)
{
    // Provider deliberately NOT wired (a tars-built NodeService shape).
    auto resp = getProof("0x" + targetAddr.hex(), {}, "latest");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(resp["error"]["message"].asString().find("Flat state reader") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
