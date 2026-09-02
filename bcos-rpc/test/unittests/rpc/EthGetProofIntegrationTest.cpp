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
 * @file EthGetProofIntegrationTest.cpp
 * @brief eth_getProof end-to-end integration (M8.4): the chain state comes from the REAL
 *        stack — FullChainFixture's real Ledger genesis + real BaselineScheduler blocks on
 *        real RocksDB — and EthEndpoint::getProof proves accounts against the trie nodes
 *        those commits actually persisted as "/mpt/" rows. Covers scenario A active
 *        (proof + independent verifyProof), scenario A dormant (-32004 "Account not in
 *        trie", EthEndpoint.cpp's EthGetProofUnavailable), and scenario B where every
 *        genesis-alloc account proves, storage slots included.
 *
 *        The node reader is a test-local snapshot: the committed "/mpt/" rows are copied
 *        from the RocksDB backend into a MemoryStorage<h256, bytes> and wrapped in the
 *        production AnyStorage type NodeService::setMPTNodeReader takes. The production
 *        reader wiring is a separate (open) PR; this test deliberately does not depend
 *        on it.
 */
#include "transaction-scheduler/tests/FullChainFixture.h"
// Shared test-asset helper (see bcos-ledger genesis tests): the SystemConfig
// predeploy alloc must carry the feature_flags Entry slot Ledger verifies.
#include "../../../../bcos-ledger/test/unittests/ledger/GenesisFeatureFlagsHelper.h"
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/hex.hpp>
#include <optional>
#include <string>
#include <vector>

namespace bcos
{
// Linkage-only definition. The BaselineScheduler instantiation inside FullChainFixture
// references bcos::unhexAddress (declared in bcos-executor/src/Common.h, pulled in by
// BaselineScheduler.h) from its getABI/getCode members — which this test never calls.
// The producing library (`executor`) is deliberately NOT linked into test-bcos-rpc:
// it PUBLIC-links wedprcrypto's Rust static libraries, whose bundled runtime breaks
// libc++ exception catching binary-wide (see bcos-rpc/test/CMakeLists.txt). This is a
// verbatim copy of bcos-executor/src/Common.cpp's definition; the linker guarantees it
// is the only one in this binary.
evmc_address unhexAddress(std::string_view view)
{
    if (view.empty())
    {
        return {};
    }
    if (view.starts_with("0x") || view.starts_with("0X"))
    {
        view = view.substr(2);
    }
    if (view.size() != sizeof(evmc_address) * 2) [[unlikely]]
    {
        return {};
    }
    evmc_address address;
    boost::algorithm::unhex(view, address.bytes);
    return address;
}
}  // namespace bcos

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

using EpiNodeStorage = storage2::memory_storage::MemoryStorage<h256, bytes>;

/// Snapshot every committed "/mpt/" row from the fixture's RocksDB backend into @p nodes —
/// the read surface a production node reader serves, minus the wiring.
void epiLoadNodes(FullChainFixture& fixture, EpiNodeStorage& nodes)
{
    task::syncWait([&]() -> task::Task<void> {
        auto iterator = co_await storage2::range(fixture.m_multiLayerStorage.latestBackend());
        while (auto keyValue = co_await iterator.next())
        {
            auto&& [key, value] = *keyValue;
            auto view = executor_v1::StateKeyView{key};
            if (view.m_table == storage2::kMPTTable)
            {
                BOOST_REQUIRE_EQUAL(view.m_key.size(), h256::SIZE);
                h256 hash{bcos::bytesConstRef(
                    reinterpret_cast<bcos::byte const*>(view.m_key.data()), h256::SIZE)};
                auto const* entry = std::get_if<storage::Entry>(std::addressof(value));
                BOOST_REQUIRE(entry != nullptr);
                auto raw = entry->get();
                co_await storage2::writeOne(nodes, hash, bytes(raw.begin(), raw.end()));
            }
        }
    }());
}

/// EthEndpoint over the fixture's REAL ledger: "latest" resolves through the real
/// getCurrentBlockNumber, the proof anchors at the real committed header's stateRoot.
struct EpiEndpointHarness
{
    rpc::NodeService::Ptr m_nodeService;
    std::unique_ptr<rpc::EthEndpoint> m_endpoint;

    EpiEndpointHarness(FullChainFixture& fixture, EpiNodeStorage& nodes)
    {
        m_nodeService = std::make_shared<rpc::NodeService>(fixture.m_ledger, nullptr,
            fixture.m_txpool, nullptr, nullptr, fixture.m_blockFactory, nullptr);
        m_nodeService->setMPTNodeReader(std::make_shared<rpc::NodeService::MPTNodeReader>(nodes));
        m_endpoint = std::make_unique<rpc::EthEndpoint>(m_nodeService, nullptr, false);
    }

    /// Direct endpoint call with EIP-1186 params; returns the result object.
    /// JsonRpcException escapes to the caller for the error-path cases.
    Json::Value getProof(
        std::string const& addressHex, std::vector<std::string> const& keys, std::string tag)
    {
        Json::Value params(Json::arrayValue);
        params.append(addressHex);
        Json::Value keysJson(Json::arrayValue);
        for (auto const& key : keys)
        {
            keysJson.append(key);
        }
        params.append(keysJson);
        params.append(std::move(tag));
        Json::Value response;
        task::syncWait(m_endpoint->getProof(params, response));
        return response["result"];
    }
};

/// A JSON quantity ("0x2a") back to trimmed big-endian bytes; "0x0" -> empty.
bytes epiQuantityToBytes(std::string const& quantity)
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

/// Rebuild the binary EIP1186Proof from the endpoint's JSON (same shape as
/// EthGetProofRpcTest's proofFromJson): hex fields back to bytes, slot values back to the
/// trie's RLP leaf encoding — verifyProof compares raw leaf bytes.
mpt::EIP1186Proof epiProofFromJson(Json::Value const& result)
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
        auto raw = epiQuantityToBytes(entryJson["value"].asString());
        entry.value = mpt::encodeStorageValue(bcos::ref(raw));
        for (auto const& node : entryJson["proof"])
        {
            entry.proof.push_back(fromHexWithPrefix(node.asString()));
        }
        out.storageProof.push_back(std::move(entry));
    }
    return out;
}

constexpr std::string_view c_epiContractAddress = "43000000000000000000000000000000000000c0";
constexpr std::string_view c_epiContractCode = "6080604052";
constexpr std::string_view c_epiEoaAddress = "1100000000000000000000000000000000000011";

std::string epiSlotHexPrefixed(char lastNibble)
{
    return "0x" + std::string(63, '0') + lastNibble;
}

BOOST_AUTO_TEST_SUITE(EthGetProofIntegrationSuite)

// Scenario A over the real chain: activation at block 2; the dormant account was written in
// the XOR era (block 1), the active account entered the MPT at block 3 (N+1).
// eth_getProof(active) returns an EIP-1186 proof the independent verifier accepts against
// the block-3 header root — and rejects against any other root. eth_getProof(dormant)
// throws the -32004 "Account not in trie" JsonRpcException.
BOOST_AUTO_TEST_CASE(ScenarioA_ActiveProofVerifies_DormantReturns32004)
{
    FullChainFixture fixture{"epi_scenario_a"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    fixture.enableFeatureFromBlock("feature_mpt_state_root", 2);

    auto const dormant = FullChainFixture::makeAddress(0xDF);
    auto const filler = FullChainFixture::makeAddress(0xF1);
    auto const active = FullChainFixture::makeAddress(0xAC);

    fixture.planBlock(1, {FullChainFixture::balanceRow(dormant, "1000000")});  // XOR era
    fixture.planBlock(2, {FullChainFixture::balanceRow(filler, "1")});  // activation block, XOR
    fixture.planBlock(3, {FullChainFixture::balanceRow(active, "2000000"),
                             FullChainFixture::nonceRow(active, "1")});  // first MPT block
    fixture.runBlock(1);
    fixture.runBlock(2);
    auto header3 = fixture.runBlock(3);

    EpiNodeStorage nodes;
    epiLoadNodes(fixture, nodes);
    EpiEndpointHarness harness{fixture, nodes};

    // (i) Active account: proof comes back and round-trips through the independent verifier.
    auto result = harness.getProof(active.hexPrefixed(), {}, "latest");
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK_EQUAL(result["balance"].asString(), "0x1e8480");  // 2000000
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x1");
    auto proof = epiProofFromJson(result);
    auto verify = mpt::verifyProof(header3->stateRoot(), proof);
    BOOST_CHECK(verify.accountValid);

    // Control: the same proof must NOT verify against a different root (block 2's XOR root).
    auto wrongRoot = fixture.headerOnChain(2)->stateRoot();
    BOOST_REQUIRE_NE(wrongRoot, header3->stateRoot());
    BOOST_CHECK(!mpt::verifyProof(wrongRoot, proof).accountValid);

    // (ii) Dormant account: explicit -32004, never an empty proof.
    try
    {
        harness.getProof(dormant.hexPrefixed(), {}, "latest");
        BOOST_FAIL("eth_getProof(dormant) must throw");
    }
    catch (rpc::JsonRpcException const& e)
    {
        BOOST_CHECK_EQUAL(e.code(), -32004);
        BOOST_CHECK(std::string(e.msg()).find("not in trie") != std::string::npos);
    }
}

// Scenario B over the real chain: every genesis-alloc account proves at the current head.
// Block 1 touches only the EOA, so the contract's proof walk crosses block-1 account-trie
// nodes AND untouched genesis storage-sub-trie nodes — both served from the committed
// "/mpt/" rows. Slot proofs recover the alloc values; an absent slot yields a valid
// exclusion proof.
BOOST_AUTO_TEST_CASE(ScenarioB_AllAllocAccountsProve)
{
    FullChainFixture fixture{"epi_scenario_b"};
    auto genesis = FullChainFixture::baseGenesis();
    genesis.m_features.push_back(
        ledger::FeatureSet{ledger::Features::Flag::feature_l2_ethereum_compat, 1});
    genesis.m_allocs.push_back(ledger::Alloc{.address = std::string(c_epiContractAddress),
        .balance = u256(500),
        .nonce = "1",
        .code = std::string(c_epiContractCode),
        .storage = {{std::string(63, '0') + "0", std::string(60, '0') + "0385"},
            {std::string(63, '0') + "2", std::string(62, '0') + "77"}}});
    genesis.m_allocs.push_back(ledger::Alloc{.address = std::string(c_epiEoaAddress),
        .balance = u256(1000),
        .nonce = "0",
        .code = "",
        .storage = {}});
    // The contract sits at the SystemConfig predeploy address: Ledger verifies
    // the feature_flags Entry slot is IN the alloc (P0: the state root commits
    // it), so the fixture must carry it like real build-allocs.py output.
    bcos::test::appendGenesisFeatureFlagsSlot(genesis);
    fixture.buildGenesis(genesis);

    Address eoa;
    boost::algorithm::unhex(c_epiEoaAddress.begin(), c_epiEoaAddress.end(), eoa.data());
    fixture.planBlock(1, {FullChainFixture::balanceRow(eoa, "2000")});
    auto header1 = fixture.runBlock(1);

    EpiNodeStorage nodes;
    epiLoadNodes(fixture, nodes);
    EpiEndpointHarness harness{fixture, nodes};

    // EOA (touched at block 1): balance updated, proof verifies at the head root.
    {
        auto result = harness.getProof("0x" + std::string(c_epiEoaAddress), {}, "latest");
        BOOST_REQUIRE(!result.isNull());
        BOOST_CHECK_EQUAL(result["balance"].asString(), "0x7d0");  // 2000
        auto verify = mpt::verifyProof(header1->stateRoot(), epiProofFromJson(result));
        BOOST_CHECK(verify.accountValid);
    }

    // Contract (untouched since genesis): account + both alloc slots + one absent slot.
    {
        auto result = harness.getProof("0x" + std::string(c_epiContractAddress),
            {epiSlotHexPrefixed('0'), epiSlotHexPrefixed('2'), epiSlotHexPrefixed('9')}, "latest");
        BOOST_REQUIRE(!result.isNull());
        BOOST_CHECK_EQUAL(result["balance"].asString(), "0x1f4");  // 500
        BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x1");
        BOOST_REQUIRE_EQUAL(result["storageProof"].size(), 3U);
        BOOST_CHECK_EQUAL(result["storageProof"][0U]["value"].asString(), "0x385");
        BOOST_CHECK_EQUAL(result["storageProof"][1U]["value"].asString(), "0x77");
        BOOST_CHECK_EQUAL(result["storageProof"][2U]["value"].asString(), "0x0");  // absent

        auto verify = mpt::verifyProof(header1->stateRoot(), epiProofFromJson(result));
        BOOST_CHECK(verify.accountValid);
        BOOST_REQUIRE_EQUAL(verify.storageValid.size(), 3U);
        BOOST_CHECK(verify.storageValid[0]);
        BOOST_CHECK(verify.storageValid[1]);
        BOOST_CHECK(verify.storageValid[2]);  // valid EXCLUSION proof
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
