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
 * @file test_GenesisEthHeader.cpp
 * @brief buildGenesisBlock A3 branch: B0 as a full Ethereum header —
 *        keccak256(rlp) hash vs an independent python reference, artifact
 *        checksum fail-fast, and the SYS_CONFIG genesis-pin relocation.
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/Exceptions.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
// Fixture values match tools/opstack-genesis/gen_eth_header_fixture.py
// (DEFAULT_FIELDS): an empty-alloc post-Karst L2 genesis. The expected hash
// was computed OFFLINE by that script (independent python rlp + keccak256);
// the test asserts the C++ RLP encoder reproduces it bit-for-bit.
constexpr std::string_view c_expectedGenesisHash =
    "b153f41d2651441ace825becbfe2f2b6bf89092864a0ae04b7e0d40a5cf64cc1";
constexpr std::string_view c_emptyTrieRoot =
    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";

ledger::EthGenesisHeader makeArtifactHeader()
{
    ledger::EthGenesisHeader header;
    header.m_parentHash = HashType{};
    header.m_sha3Uncles =
        HashType("1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    header.m_miner = Address("4200000000000000000000000000000000000011");
    header.m_stateRoot = HashType(std::string(c_emptyTrieRoot));
    header.m_transactionsRoot = HashType(std::string(c_emptyTrieRoot));
    header.m_receiptsRoot = HashType(std::string(c_emptyTrieRoot));
    header.m_logsBloom = bcos::bytes(256, 0);
    header.m_difficulty = 0;
    header.m_number = 0;
    header.m_gasLimit = 30'000'000;
    header.m_gasUsed = 0;
    header.m_timestamp = 0x689d5c00;
    // Jovian 17-byte extraData: version || denominator || elasticity || minBaseFee
    header.m_extraData = fromHex(std::string("00000000fa000000060000000000000000"));
    header.m_mixHash = h256{};
    header.m_nonce = h64{};
    header.m_baseFeePerGas = 1'000'000'000;
    header.m_withdrawalsRoot = HashType(std::string(c_emptyTrieRoot));
    header.m_blobGasUsed = 0;
    header.m_excessBlobGas = 0;
    header.m_parentBeaconBlockRoot = h256{};
    header.m_requestsHash =
        HashType("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    header.m_hash = HashType(std::string(c_expectedGenesisHash));
    return header;
}

GenesisConfig makeEthGenesisConfig()
{
    GenesisConfig genesisConfig;
    genesisConfig.m_txGasLimit = 3000000000;
    genesisConfig.m_compatibilityVersion =
        static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
    genesisConfig.m_features.push_back(FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
    genesisConfig.m_chainID = "901";
    genesisConfig.m_groupID = "group0";
    // Empty allocs on purpose: the empty-alloc L2 branch publishes
    // mpt::emptyRootHash(), which is exactly the fixture's state_root — so the
    // whole 21-field header is deterministic and the offline python hash
    // applies. (NodeConfig::validateL2Invariants rejects empty-alloc L2
    // configs, but buildGenesisBlock is callable directly.)
    genesisConfig.m_ethGenesisHeader = makeArtifactHeader();
    return genesisConfig;
}

struct EthGenesisFixture
{
    EthGenesisFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisEthHeaderTest, EthGenesisFixture)

BOOST_AUTO_TEST_CASE(BuildsRlpHashMatchingPythonReference)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto genesisConfig = makeEthGenesisConfig();
        auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);
        BOOST_CHECK(ok);

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        auto header = block->blockHeader();

        // B0's hash is keccak256(rlp(header)) and equals the offline python
        // reference — the determinism vector for the whole A3 pipeline.
        BOOST_CHECK_EQUAL(header->hash().hex(), std::string(c_expectedGenesisHash));
        // extraData carries the artifact's 17-byte Jovian payload, NOT the
        // FISCO genesisData text.
        BOOST_CHECK_EQUAL(header->extraData().size(), 17U);
        // The stored header keeps its Ethereum identity across encode/decode.
        BOOST_CHECK(header->ethBlockVersion() == EthBlockVersion::PRAGUE);
        BOOST_CHECK_EQUAL(header->stateRoot().hex(), std::string(c_emptyTrieRoot));
        // B0 timestamp is stored in the internal MILLISECOND domain (artifact
        // seconds x 1000); the RLP bridge divides back to seconds for the hash.
        BOOST_CHECK_EQUAL(header->timestamp(), 0x689d5c00LL * 1000);

        // The FISCO genesis pin moved to SYS_CONFIG.
        auto pinEntry = co_await storage2::readOne(
            *storage, executor_v1::StateKeyView(SYS_CONFIG, INTERNAL_SYSTEM_KEY_ETH_GENESIS_DATA));
        BOOST_REQUIRE(pinEntry);
        auto [pinnedData, enableNumber] =
            bcos::storage::serialize::decode<SystemConfigEntry>(pinEntry->get());
        BOOST_CHECK(!pinnedData.empty());
        BOOST_CHECK(pinnedData.find("[ethGenesisHeader]") != std::string::npos);
        BOOST_CHECK_EQUAL(enableNumber, 0);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(RestartWithSameArtifactSucceeds)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto genesisConfig = makeEthGenesisConfig();
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));
        // Second run = node restart: the stored pin and B0 hash both match.
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(WrongArtifactHashRefusesToBuild)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto genesisConfig = makeEthGenesisConfig();
        genesisConfig.m_ethGenesisHeader->m_hash.data()[31] ^= 0x01;  // corrupt the checksum
        BOOST_CHECK_THROW(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param),
            bcos::tool::InvalidConfig);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(TamperedFieldChangesHashAndRefuses)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        // One field off (gasLimit + 1) with the original expected hash: the
        // recomputed keccak(rlp) no longer matches the checksum.
        auto genesisConfig = makeEthGenesisConfig();
        genesisConfig.m_ethGenesisHeader->m_gasLimit += 1;
        BOOST_CHECK_THROW(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param),
            bcos::tool::InvalidConfig);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(ArtifactStateRootMustMatchDerivedRoot)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto genesisConfig = makeEthGenesisConfig();
        genesisConfig.m_ethGenesisHeader->m_stateRoot.data()[0] ^= 0x01;
        BOOST_CHECK_THROW(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param),
            bcos::tool::InvalidConfig);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(CorrectedArtifactRetriesOnSameStorage)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        // First attempt with a drifted artifact fails BEFORE any table/state
        // write; fixing the config and retrying on the SAME storage must then
        // succeed (a mis-configured artifact must not brick the datadir).
        auto drifted = makeEthGenesisConfig();
        drifted.m_ethGenesisHeader->m_hash.data()[31] ^= 0x01;
        BOOST_CHECK_THROW(
            co_await ledger::buildGenesisBlock(*ledger, drifted, param), bcos::tool::InvalidConfig);

        auto corrected = makeEthGenesisConfig();
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, corrected, param));
        // And a restart after the successful retry still passes.
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, corrected, param));
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(RestartWithSwappedArtifactRefuses)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto genesisConfig = makeEthGenesisConfig();
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));

        // Restart against a different artifact claim: the stored B0 must win.
        auto swapped = makeEthGenesisConfig();
        swapped.m_ethGenesisHeader->m_hash.data()[0] ^= 0x01;
        BOOST_CHECK_THROW(
            co_await ledger::buildGenesisBlock(*ledger, swapped, param), bcos::tool::InvalidConfig);
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
