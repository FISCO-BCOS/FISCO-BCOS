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
 * @file test_LedgerGenesisAllocsHash.cpp
 * @brief Second-startup guard over the genesis allocs: the op-geth state root is
 *        persisted on first init and re-derived on every subsequent startup, so
 *        any alloc drift (or removing the feature) refuses to start.
 *        "Restart" is simulated by calling buildGenesisBlock a second time on
 *        the same StateStorage: the second call sees the existing genesis
 *        block and enters the verify path.
 */
#include "GenesisFeatureFlagsHelper.h"
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage2/Storage.h"
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
using namespace bcos::storage;
using namespace bcos::crypto;
using namespace std::string_literals;

namespace bcos::test
{
namespace
{
struct AllocsHashFixture
{
    AllocsHashFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    std::shared_ptr<L2GenesisTestStorage> makeStorage() { return makeL2GenesisTestStorage(); }

    LedgerConfig makeParam()
    {
        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);
        return param;
    }

    GenesisConfig makeL2Config()
    {
        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
        genesisConfig.m_chainID = "901";
        genesisConfig.m_groupID = "group0";
        genesisConfig.m_allocs.push_back(
            Alloc{.address = "43000000000000000000000000000000000000c0",
                .balance = u256(0),
                .nonce = "0",
                .code = "6080604052",
                .storage = {}});
        appendGenesisFeatureFlagsSlot(genesisConfig);
        return genesisConfig;
    }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(LedgerGenesisAllocsHashTest, AllocsHashFixture)

BOOST_AUTO_TEST_CASE(SecondStartupSameConfigOk)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto config = makeL2Config();

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, config, param));

        // identical config on the same storage -> idempotent, hash matches
        bool secondOk = co_await ledger::buildGenesisBlock(*ledger, config, param);
        BOOST_CHECK(secondOk);
    }());
}

BOOST_AUTO_TEST_CASE(SecondStartupChangedChainModeAborts)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto config = makeL2Config();

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, config, param));

        // flip off feature_l2_ethereum_compat but keep allocs so the guard
        // (allocs non-empty) still fires. NodeConfig would reject this upstream
        // (validateL2Invariants requires the two to agree); Ledger defends anyway.
        auto changed = config;
        changed.m_features.clear();

        BOOST_CHECK_THROW(
            co_await ledger::buildGenesisBlock(*ledger, changed, param), bcos::tool::InvalidConfig);
    }());
}

BOOST_AUTO_TEST_CASE(SecondStartupChangedAllocAborts)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto config = makeL2Config();

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, config, param));

        // flip a single code byte of the alloc -> immutable hash mismatch
        auto changed = config;
        changed.m_allocs[0].code = "6080604053";  // last byte 52 -> 53

        BOOST_CHECK_THROW(
            co_await ledger::buildGenesisBlock(*ledger, changed, param), bcos::tool::InvalidConfig);
    }());
}

// unhex into the fixed 20/32-byte buffers requires an exact digit count: over-long
// input would write PAST the buffer and short input would silently zero-pad — both
// must fail loudly instead of corrupting the genesis state. NodeConfig::loadAllocs
// guards the INI path; these drive buildGenesisBlock directly.
BOOST_AUTO_TEST_CASE(MalformedAllocHexAborts)
{
    task::syncWait([this]() -> task::Task<void> {
        auto param = makeParam();

        // short address (38 hex digits)
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto config = makeL2Config();
            config.m_allocs[0].address = "430000000000000000000000000000000000c0";
            appendGenesisFeatureFlagsSlot(config);
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
        }
        // over-long address (42 hex digits)
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto config = makeL2Config();
            config.m_allocs[0].address = "4300000000000000000000000000000000000000c0";
            appendGenesisFeatureFlagsSlot(config);
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
        }
        // short storage slot value (2 hex digits)
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto config = makeL2Config();
            config.m_allocs[0].storage = {{"01", "02"}};
            appendGenesisFeatureFlagsSlot(config);
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
        }
        // over-long storage slot key (66 hex digits)
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto config = makeL2Config();
            config.m_allocs[0].storage = {{
                "000000000000000000000000000000000000000000000000000000000000000065", "02"}};
            appendGenesisFeatureFlagsSlot(config);
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
        }
        // correct length but non-hex charset ('g') — must surface as InvalidConfig,
        // not the raw boost::algorithm::non_hex_input
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto config = makeL2Config();
            config.m_allocs[0].storage = {{std::string(64, '0'), std::string(63, '0') + "g"}};
            appendGenesisFeatureFlagsSlot(config);
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
        }
        // feature_flags slot VALUE mismatch (valid hex, wrong content): the
        // verification runs before the first write, so the rejected config
        // leaves no account rows behind — not even the SystemConfig account's
        // s_tables registration.
        {
            auto storage = makeStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            // makeL2Config already carries the (correct) feature_flags slot;
            // corrupt its last hex digit.
            auto config = makeL2Config();
            auto& flagsValue = config.m_allocs[0].storage.back().second;
            flagsValue.back() = (flagsValue.back() == '0' ? '1' : '0');
            BOOST_CHECK_THROW(
                co_await ledger::buildGenesisBlock(*ledger, config, param),
                bcos::tool::InvalidConfig);
            auto tableEntry = co_await storage2::readOne(*storage,
                executor_v1::StateKeyView(std::string(ledger::SYS_TABLES),
                    std::string(ledger::SYS_DIRECTORY::USER_APPS) +
                        "43000000000000000000000000000000000000c0"));
            BOOST_CHECK(!tableEntry);
        }
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
