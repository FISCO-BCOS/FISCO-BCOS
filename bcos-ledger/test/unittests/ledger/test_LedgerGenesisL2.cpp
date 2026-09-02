/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file test_LedgerGenesisL2.cpp
 * @brief buildGenesisBlock L2 branch: allocs flat-KV writes + persisted op-geth
 *        genesis state root row.
 */
#include "GenesisFeatureFlagsHelper.h"
#include "L2GenesisTestStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/FeaturesStorage.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/GenesisStateLoader.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/test/unittests/ExceptionCheck.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::crypto;
using namespace std::string_literals;

namespace bcos::test
{
namespace
{
struct L2GenesisFixture
{
    L2GenesisFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    std::shared_ptr<L2GenesisTestStorage> makeStorage() { return makeL2GenesisTestStorage(); }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(LedgerGenesisL2Test, L2GenesisFixture)

BOOST_AUTO_TEST_CASE(L2BranchWritesAllocsToFlatKV)
{
    task::syncWait([this]() -> task::Task<void> {
        auto hashImpl = std::make_shared<Keccak256>();
        auto storage = makeStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
        genesisConfig.m_chainID = "901";
        genesisConfig.m_groupID = "group0";

        // single predeploy: address 0x43..00c0, 5-byte code, one storage slot
        std::string address = "43000000000000000000000000000000000000c0";
        std::string code = "6080604052";             // hex of contract bytecode (5 bytes)
        std::string slotKey = std::string(64, '0');  // slot 0x00..00
        std::string slotValue = std::string(60, '0') + "0385";

        genesisConfig.m_allocs.push_back(Alloc{.address = address,
            .balance = u256(0),
            .nonce = "0",
            .code = code,
            .storage = {{slotKey, slotValue}}});
        appendGenesisFeatureFlagsSlot(genesisConfig);

        co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);

        // 1) code binary is readable by its keccak hash via the account table
        auto tableName = fmt::format("{}{}", SYS_DIRECTORY::USER_APPS, address);
        auto codeHashEntry = co_await storage2::readOne(
            *storage, executor_v1::StateKeyView(tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH));
        BOOST_REQUIRE(codeHashEntry);

        auto codeEntry = co_await storage2::readOne(
            *storage, executor_v1::StateKeyView(ledger::SYS_CODE_BINARY, codeHashEntry->get()));
        BOOST_REQUIRE(codeEntry);

        bcos::bytes expectedCode;
        boost::algorithm::unhex(code, std::back_inserter(expectedCode));
        BOOST_CHECK_EQUAL(codeEntry->get(),
            std::string_view((const char*)expectedCode.data(), expectedCode.size()));

        // codeHash stored equals keccak256(code bytes)
        auto computedCodeHash =
            hashImpl->hash(bcos::bytesConstRef(expectedCode.data(), expectedCode.size()));
        auto computedCodeHashBytes = computedCodeHash.asBytes();
        BOOST_CHECK_EQUAL(
            codeHashEntry->get(), std::string_view((const char*)computedCodeHashBytes.data(),
                                      computedCodeHashBytes.size()));

        // 2) storage slot is readable with the written value (32-byte raw key)
        bcos::bytes slotKeyBytes;
        boost::algorithm::unhex(slotKey, std::back_inserter(slotKeyBytes));
        auto slotEntry = co_await storage2::readOne(
            *storage, executor_v1::StateKeyView(tableName,
                          std::string_view((const char*)slotKeyBytes.data(), slotKeyBytes.size())));
        BOOST_REQUIRE(slotEntry);
        bcos::bytes expectedSlotValue;
        boost::algorithm::unhex(slotValue, std::back_inserter(expectedSlotValue));
        BOOST_CHECK_EQUAL(slotEntry->get(),
            std::string_view((const char*)expectedSlotValue.data(), expectedSlotValue.size()));

        // 3) op-geth genesis state root carried in the genesis block header's
        // stateRoot, equal to the standalone computeGenesisStateRoot over the
        // same allocs. (This is the value the Engine-API eth-block view serves
        // as the genesis stateRoot, and the alloc-immutability guard re-derives
        // on restart by reading it back from the header.)
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        auto expectedRoot = co_await ledger::computeGenesisStateRoot(genesisConfig);
        BOOST_CHECK_EQUAL(block->blockHeader()->stateRoot(), expectedRoot);

        // 4) L2 feature flag persisted
        ledger::Features features;
        co_await features.readFromStorage(*storage, 0);
        BOOST_CHECK(features.get(ledger::Features::Flag::feature_l2_ethereum_compat));
    }());
}

BOOST_AUTO_TEST_CASE(PbftBranchUnchanged)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        // pbft mode: no feature_l2_ethereum_compat, no allocs
        // (validateL2Invariants requires they agree)

        auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param);
        BOOST_CHECK(ok);

        // pbft mode leaves the genesis block stateRoot empty (no allocs; the
        // eth-block view is L2-only), as it was before this feature.
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        BOOST_CHECK_EQUAL(block->blockHeader()->stateRoot(), bcos::crypto::HashType());

        // pbft mode must NOT enable the L2 feature flag
        ledger::Features features;
        co_await features.readFromStorage(*storage, 0);
        BOOST_CHECK(!features.get(ledger::Features::Flag::feature_l2_ethereum_compat));
    }());
}

// A bad hex field anywhere in the alloc set must abort BEFORE any row is
// written: genesis import is not transactional, and the loader runs the
// validating computeGenesisStateTrie pass before the first create(), so a
// malformed alloc — even in a LATER entry — leaves no /apps/ rows behind.
BOOST_AUTO_TEST_CASE(ImportValidatesAllocHexBeforeFirstWrite)
{
    task::syncWait([this]() -> task::Task<void> {
        auto hashImpl = std::make_shared<Keccak256>();
        auto storage = makeStorage();

        ledger::Features features;
        features.set(Features::Flag::feature_raw_address);

        std::string goodAddress = "43000000000000000000000000000000000000c0";
        std::string badAddress = "43000000000000000000000000000000000000c1";
        std::vector<Alloc> allocs;
        allocs.push_back(Alloc{.address = goodAddress,
            .balance = u256(1),
            .nonce = "1",
            .code = "6080604052",
            .storage = {{std::string(64, '0'), std::string(64, '1')}}});
        // short storage slot value (2 hex digits) in the SECOND alloc
        allocs.push_back(Alloc{.address = badAddress,
            .balance = u256(2),
            .nonce = "2",
            .code = "",
            .storage = {{std::string(64, '0'), "01"}}});

        BOOST_CHECK_EXCEPTION(
            co_await importEthereumGenesisState(*storage, allocs, *hashImpl, features),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "storage slot value must be exactly 64 hex digits");
            });

        // Nothing may be written at all: not the offending account, and not the
        // well-formed FIRST account either (the validating trie pass runs before
        // the first create()).
        for (auto const& address : {goodAddress, badAddress})
        {
            auto table = fmt::format("{}{}", SYS_DIRECTORY::USER_APPS, address);
            auto createRow =
                co_await storage2::readOne(*storage, executor_v1::StateKeyView(SYS_TABLES, table));
            BOOST_CHECK(!createRow);
            auto codeHashRow = co_await storage2::readOne(
                *storage, executor_v1::StateKeyView(table, ACCOUNT_TABLE_FIELDS::CODE_HASH));
            BOOST_CHECK(!codeHashRow);
        }

        // A malformed nonce aborts with the same field-naming InvalidConfig
        // contract (not an unnamed boost::bad_lexical_cast), also before any write.
        std::vector<Alloc> badNonceAllocs;
        badNonceAllocs.push_back(Alloc{.address = goodAddress,
            .balance = u256(1),
            .nonce = "abc",  // not a decimal uint64
            .code = "6080604052",
            .storage = {{std::string(64, '0'), std::string(64, '1')}}});
        BOOST_CHECK_EXCEPTION(
            co_await importEthereumGenesisState(*storage, badNonceAllocs, *hashImpl, features),
            bcos::tool::InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "nonce is not a valid uint64"); });
        auto badNonceRow = co_await storage2::readOne(*storage, executor_v1::StateKeyView(
                                                                     SYS_TABLES,
                                                                     std::string(SYS_DIRECTORY::
                                                                             USER_APPS) +
                                                                         goodAddress));
        BOOST_CHECK(!badNonceRow);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
