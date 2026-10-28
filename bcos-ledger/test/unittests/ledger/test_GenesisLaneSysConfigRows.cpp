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
 * @file test_GenesisLaneSysConfigRows.cpp
 * @brief The SYS_CONFIG rows buildGenesisBlock writes per lane: v0/v1 keep the
 *        Features / compatibility_version rows they always had, v2/v3 write none of them.
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/NodeConfig.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <set>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
// The 3.18.0 v1 consortium genesis with every Features-touching knob on: rpbft writes
// feature_rpbft, [features] writes feature_evm_cancun, compat 3.18 writes the compat
// defaults. The row set it produces is the pre-cleanup baseline.
GenesisConfig makeV1Genesis()
{
    GenesisConfig genesis;
    genesis.m_txGasLimit = 3000000000;
    genesis.m_compatibilityVersion = static_cast<uint32_t>(BlockVersion::V3_18_0_VERSION);
    genesis.m_executorVersion = 1;
    genesis.m_chainID = "chain0";
    genesis.m_groupID = "group0";
    genesis.m_web3ChainID = "20200";
    genesis.m_consensusType = std::string(RPBFT_CONSENSUS_TYPE);
    genesis.m_isAuthCheck = true;
    genesis.m_authAdminAccount = "0x1111111111111111111111111111111111111111";
    genesis.m_features = {FeatureSet{.flag = Features::Flag::feature_evm_cancun, .enable = 1}};
    return genesis;
}

// Captured on the pre-cleanup baseline (0de81a31b) from makeV1Genesis().
const std::set<std::string> c_v1BaselineKeys = {
    "auth_check_status",
    "balance_transfer",
    "bugfix_auth_check",
    "bugfix_call_noaddr_return",
    "bugfix_delegatecall_transfer",
    "bugfix_dmc_create_address_rule",
    "bugfix_delete_account_code",
    "bugfix_dmc_deploy_gas_used",
    "bugfix_dmc_revert",
    "bugfix_eip161_1052_account_semantics",
    "bugfix_eip55_addr",
    "bugfix_empty_abi_reset",
    "bugfix_eoa_as_contract",
    "bugfix_eoa_match_failed",
    "bugfix_event_log_order",
    "bugfix_evm_create2_delegatecall_staticcall_codecopy",
    "bugfix_baseline_status_mapper_ordinary_failures",
    "bugfix_evm2929_created_address_warm",
    "bugfix_evm_exception_gas_used",
    "bugfix_evm_storage_status",
    "bugfix_gas_payment_balance_precheck",
    "bugfix_internal_create_seq",
    "bugfix_precompiled_node_fault_rethrow",
    "bugfix_internal_create_permission_denied",
    "bugfix_internal_create_redundant_storage",
    "bugfix_keypage_system_entry_hash",
    "bugfix_method_auth_sender",
    "bugfix_nonce_initialize",
    "bugfix_nonce_not_increase_when_revert",
    "bugfix_nonce_ordering",
    "bugfix_policy1_empty_code_address",
    "bugfix_precompiled_codehash",
    "bugfix_precompiled_evm_status",
    "bugfix_precompiled_feature_gate",
    "bugfix_precompiled_gascalc",
    "bugfix_precompiled_gasused",
    "bugfix_revert",
    "bugfix_revert_logs",
    "bugfix_rpbft_vrf_blocknumber_input",
    "bugfix_set_contract_nonce_when_create",
    "bugfix_set_row_with_dirty_flag",
    "bugfix_sharding_call_in_child_executive",
    "bugfix_statestorage_hash",
    "bugfix_statestorage_hash_v3_17",
    "bugfix_staticcall_noaddr_return",
    "bugfix_support_transfer_receive_fallback",
    "bugfix_v1_error_handling",
    "bugfix_v1_timestamp",
    "compatibility_version",
    "consensus_leader_period",
    "executor_version",
    "feature_evm_cancun",
    "feature_rpbft",
    "feature_rpbft_epoch_block_num",
    "feature_rpbft_epoch_sealer_num",
    "feature_rpbft_notify_rotate",
    "tx_count_limit",
    "tx_gas_limit",
    "tx_gas_price",
    "web3_chain_id",
};

// The same knobs on an Ethereum-lane genesis (pbft: NodeConfig refuses rpbft there): none of
// them may reach SYS_CONFIG.
GenesisConfig makeEthLaneGenesis(int executorVersion)
{
    GenesisConfig genesis = makeV1Genesis();
    genesis.m_executorVersion = executorVersion;
    genesis.m_chainID = "901";
    genesis.m_web3ChainID = "901";
    genesis.m_consensusType = std::string(PBFT_CONSENSUS_TYPE);
    genesis.m_isAuthCheck = false;
    genesis.m_authAdminAccount.clear();
    return genesis;
}

std::set<std::string> sysConfigKeys(storage::StorageInterface& storage)
{
    std::promise<std::vector<std::string>> promise;
    storage.asyncGetPrimaryKeys(SYS_CONFIG, std::nullopt,
        [&promise](Error::UniquePtr error, std::vector<std::string> keys) {
            BOOST_REQUIRE(!error);
            promise.set_value(std::move(keys));
        });
    auto keys = promise.get_future().get();
    return {keys.begin(), keys.end()};
}

LedgerConfig genesisParam()
{
    LedgerConfig param;
    param.setBlockNumber(0);
    param.setHash(HashType(""));
    param.setBlockTxCountLimit(0);
    return param;
}

struct GenesisLaneFixture
{
    GenesisLaneFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisLaneSysConfigRowsTest, GenesisLaneFixture)

BOOST_AUTO_TEST_CASE(V1WritesBaselineRowSet)
{
    auto storage = makeL2GenesisTestStorage();
    auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
    auto param = genesisParam();
    auto genesis = makeV1Genesis();
    BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));

    auto const keys = sysConfigKeys(*storage);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        keys.begin(), keys.end(), c_v1BaselineKeys.begin(), c_v1BaselineKeys.end());
}

BOOST_AUTO_TEST_CASE(EthLanesWriteNoFeaturesOrCompatRows)
{
    for (int executorVersion : {ETHEREUM_EXECUTOR_VERSION, OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto param = genesisParam();
            auto genesis = makeEthLaneGenesis(executorVersion);
            BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));
            // Restart with the same config: the binary-swap branch must not backfill the
            // sharding-default Features either.
            BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));

            auto const keys = sysConfigKeys(*storage);
            BOOST_CHECK(
                keys.contains(std::string(magic_enum::enum_name(SystemConfig::executor_version))));
            BOOST_CHECK(!keys.contains(std::string(SYSTEM_KEY_COMPATIBILITY_VERSION)));
            for (auto const& key : keys)
            {
                BOOST_CHECK_MESSAGE(!key.starts_with("feature_") && !key.starts_with("bugfix_"),
                    "unexpected Features row " << key);
            }
        }
    }
}

// A v2/v3 datadir initialized before the cleanup carries a compatibility_version row and a
// current block number. Restarting it must not take the v0/v1 binary-swap branch, which
// would backfill the sharding-default Features rows from that compat row.
BOOST_AUTO_TEST_CASE(EthLaneRestartOnPreCleanupDatadirBackfillsNoFeatures)
{
    for (int executorVersion : {ETHEREUM_EXECUTOR_VERSION, OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto param = genesisParam();
            auto genesis = makeEthLaneGenesis(executorVersion);
            BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));

            storage::Entry compatEntry;
            compatEntry.set(
                bcos::storage::serialize::encode(SystemConfigEntry{std::string("3.18.0"), 0}));
            storage->asyncSetRow(SYS_CONFIG, SYSTEM_KEY_COMPATIBILITY_VERSION,
                std::move(compatEntry), [](Error::UniquePtr) {});
            storage::Entry numberEntry;
            numberEntry.set("0");
            storage->asyncSetRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry),
                [](Error::UniquePtr) {});

            BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));

            for (auto const& key : sysConfigKeys(*storage))
            {
                BOOST_CHECK_MESSAGE(!key.starts_with("feature_") && !key.starts_with("bugfix_"),
                    "backfilled Features row " << key);
            }
        }
    }
}

// Without the compatibility_version row, the two runtime readers fall back to the lane
// constant: the key-page state storage (v3 boot probe, eth_getStorageAt) and getLedgerConfig
// (block header version on the v2 Engine / verifier paths).
BOOST_AUTO_TEST_CASE(EthLanesReadLaneCompatibilityVersion)
{
    for (int executorVersion : {ETHEREUM_EXECUTOR_VERSION, OPSTACK_EXECUTOR_VERSION})
    {
        BOOST_TEST_CONTEXT("executor.version=" << executorVersion)
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto param = genesisParam();
            auto genesis = makeEthLaneGenesis(executorVersion);
            BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, genesis, param)));

            ledger->setKeyPageSize(10240);
            BOOST_CHECK(ledger->getStateStorage());

            LedgerConfig config;
            task::syncWait(ledger::getLedgerConfig(*ledger, config));
            BOOST_CHECK_EQUAL(config.compatibilityVersion(), ETHEREUM_LANE_COMPATIBILITY_VERSION);
        }
    }
}

// The genesis [features] dependency check runs on first init only. A v1 chain initialized
// before the rule existed (here: feature_evm_prague without feature_evm_cancun, simulated by
// rewriting the pin in B0's extraData) must still restart with its unchanged config.
BOOST_AUTO_TEST_CASE(V1RestartSkipsGenesisFeatureDependencyCheck)
{
    auto storage = makeL2GenesisTestStorage();
    auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
    auto param = genesisParam();

    auto valid = makeV1Genesis();
    valid.m_features = {FeatureSet{.flag = Features::Flag::feature_evm_cancun, .enable = 1},
        FeatureSet{.flag = Features::Flag::feature_evm_prague, .enable = 1}};
    BOOST_REQUIRE(task::syncWait(ledger::buildGenesisBlock(*ledger, valid, param)));

    auto legacy = makeV1Genesis();
    legacy.m_features = {FeatureSet{.flag = Features::Flag::feature_evm_prague, .enable = 1}};
    auto legacyPin = bcos::tool::generateGenesisData(legacy, param);
    auto block = task::syncWait(ledger::getBlockData(*ledger, 0, HEADER));
    BOOST_REQUIRE(block);
    auto header = block->blockHeader();
    header->setExtraData(bcos::bytes(legacyPin.begin(), legacyPin.end()));
    bytes encoded;
    header->encode(encoded);
    storage::Entry headerEntry;
    headerEntry.set(std::move(encoded));
    storage->asyncSetRow(
        SYS_NUMBER_2_BLOCK_HEADER, "0", std::move(headerEntry), [](Error::UniquePtr) {});

    BOOST_CHECK(task::syncWait(ledger::buildGenesisBlock(*ledger, legacy, param)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
