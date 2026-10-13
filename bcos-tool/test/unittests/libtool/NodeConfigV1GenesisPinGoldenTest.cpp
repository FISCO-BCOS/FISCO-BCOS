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
 * @file NodeConfigV1GenesisPinGoldenTest.cpp
 * @brief Byte-exact golden values for the genesis pin of FISCO (v0/v1 lane) chains.
 */

// Ledger::buildGenesisBlock byte-compares generateGenesisData's output against the pin stored at
// block 0, so a chain restarts on a new binary only if that string is unchanged. The goldens
// below were generated on upstream/release-3.18.0 @ 33af5d95a, before any config-cleanup change.
// Never regenerate them to make this test pass: a mismatch means an existing (or 3.18-born) FISCO
// chain would refuse to start.
//
// Why loadGenesisConfigFromString is the whole input: a node runs loadGenesisConfig and then
// loadConfig(config.ini) (Initializer.cpp, AirNodeInitializer.cpp), and LedgerInitializer hands
// genesisConfig()/ledgerConfig() to buildGenesisBlock unchanged. The only config.ini loader that
// writes a pin field is loadChainConfig (sm_crypto/group_id/chain_id), and it runs only below
// compat 3.1, where the legacy pin does not contain those three fields.
//
// What the legacy (< 3.1) rows do not exercise: that pin carries no sm_crypto, consensus_type or
// [features], so those columns only show that such genesis files still load. executor.version
// never reaches the pin at any compat; its column makes a change that starts pinning it go red.
//
// Baseline quirks locked on purpose:
// - every genesis sets leader_period=5, yet sectioned (>= 3.1) pins print leader_period:1 while
//   the legacy pin prints 5 (#5690);
// - pbft genesis files set epoch_sealer_num=7/epoch_block_num=333, yet sectioned pins at
//   compat >= 3.5 print the defaults 4/1000, because the loader reads the epoch keys only for
//   rpbft.
//
// Left out deliberately: executor.evm_revision(_forks), tx.gas_price and tx.excess_blob_gas reach
// a sectioned pin, but no released binary (3.16.0-3.17.1) emits them for a FISCO chain, so no
// existing v0/v1 pin contains them. is_wasm=true is rejected at load. v2/v3 pins are out of scope.

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <array>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigV1GenesisPinGoldenTest)

namespace
{
struct PinCase
{
    std::string_view name;
    std::string_view compat;
    std::string_view consensusType;  // pbft | rpbft
    bool withFeatures;
    bool smCrypto;
    bool authCheck;
    int executorVersion;      // 0 = key absent, 1 = executor.version=1
    bool onlyCompatAndNodes;  // write nothing else, so every other pin field is a loader default
    std::string_view golden;
};

// Two nodes listed out of order with different weights, so the pin also locks the node sort and
// the weight encoding.
constexpr std::string_view c_nodeA =
    "bbbb567890123456789012345678901234567890123456789012345678901234"
    "1234567890123456789012345678901234567890123456789012345678901234";
constexpr std::string_view c_nodeB =
    "aaaa567890123456789012345678901234567890123456789012345678901234"
    "1234567890123456789012345678901234567890123456789012345678901234";

std::string v1Genesis(PinCase const& pinCase)
{
    std::string genesis;
    genesis += "[version]\ncompatibility_version=" + std::string(pinCase.compat) + "\n";
    if (pinCase.onlyCompatAndNodes)
    {
        genesis += "[consensus]\nnode.0=" + std::string(c_nodeA) + ":1\n";
        genesis += "node.1=" + std::string(c_nodeB) + ":3\n";
        return genesis;
    }
    genesis += "[chain]\nsm_crypto=" + std::string(pinCase.smCrypto ? "true" : "false") +
               "\ngroup_id=group0\nchain_id=chain0\n";
    genesis += "[consensus]\nconsensus_type=" + std::string(pinCase.consensusType) +
               "\nblock_tx_count_limit=1000\nleader_period=5\n";
    genesis += pinCase.consensusType == "rpbft" ? "epoch_sealer_num=2\nepoch_block_num=500\n" :
                                                  "epoch_sealer_num=7\nepoch_block_num=333\n";
    genesis += "node.0=" + std::string(c_nodeA) + ":1\n";
    genesis += "node.1=" + std::string(c_nodeB) + ":3\n";
    genesis += "[tx]\ngas_limit=300000000\n";
    genesis += "[executor]\nis_wasm=false\nis_serial_execute=true\n";
    genesis += "is_auth_check=" + std::string(pinCase.authCheck ? "true" : "false") + "\n";
    genesis += "auth_admin_account=0x1111111111111111111111111111111111111111\n";
    if (pinCase.executorVersion != 0)
    {
        genesis += "version=" + std::to_string(pinCase.executorVersion) + "\n";
    }
    if (pinCase.withFeatures)
    {
        // Enabled and disabled entries, bugfix_* and feature_* names from across the enum (EVM
        // flags and the deprecated feature_raw_address included), in non-enum order. All of
        // them exist in v3.17.1.
        genesis +=
            "[features]\nfeature_balance=1\nbugfix_revert=1\nfeature_sharding=0\n"
            "feature_rpbft=1\nbugfix_auth_check=1\nfeature_evm_cancun=1\n"
            "feature_balance_policy2=1\nfeature_raw_address=1\nfeature_rpbft_term_weight=0\n";
    }
    return genesis;
}

// clang-format off
// | case            | compat | consensus | features | sm  | auth | executor.version | only compat+nodes |
// |-----------------|--------|-----------|----------|-----|------|------------------|-------------------|
// | 3.0-min         | 3.0    | default   | -        | -   | -    | absent           | yes               |
// | 3.0-pbft        | 3.0    | pbft      | -        | -   | -    | absent           | -                 |
// | 3.0-rpbft       | 3.0    | rpbft     | yes      | yes | yes  | absent           | -                 |
// | 3.1-min         | 3.1    | default   | -        | -   | -    | absent           | yes               |
// | 3.1-pbft        | 3.1    | pbft      | -        | -   | -    | absent           | -                 |
// | 3.1-rpbft       | 3.1    | rpbft     | yes      | yes | -    | absent           | -                 |
// | 3.6-pbft        | 3.6    | pbft      | yes      | -   | yes  | absent           | -                 |
// | 3.6-rpbft       | 3.6    | rpbft     | -        | yes | -    | absent           | -                 |
// | 3.17-pbft       | 3.17   | pbft      | -        | -   | -    | 1                | -                 |
// | 3.17-rpbft      | 3.17   | rpbft     | yes      | yes | yes  | absent           | -                 |
// | 3.18-pbft       | 3.18   | pbft      | yes      | -   | -    | 1                | -                 |
// | 3.18-rpbft      | 3.18   | rpbft     | -        | yes | yes  | absent           | -                 |
// | 3.18-pbft-all   | 3.18   | pbft      | yes      | yes | yes  | 1                | -                 |
// | 3.18-pbft-plain | 3.18   | pbft      | -        | -   | -    | absent           | -                 |
// clang-format on
constexpr auto c_cases = std::to_array<PinCase>({
    PinCase{
        .name = "3.0-min",
        .compat = "3.0.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = true,
        .golden =
            R"pin(1000-1-3000000000-3.0.0-0-0--0aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3;bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1;)pin",
    },
    PinCase{
        .name = "3.0-pbft",
        .compat = "3.0.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden =
            R"pin(1000-5-300000000-3.0.0-0-0-0x1111111111111111111111111111111111111111-1aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3;bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1;)pin",
    },
    PinCase{
        .name = "3.0-rpbft",
        .compat = "3.0.0",
        .consensusType = "rpbft",
        .withFeatures = true,
        .smCrypto = true,
        .authCheck = true,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden =
            R"pin(1000-5-300000000-3.0.0-0-1-0x1111111111111111111111111111111111111111-1aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3;bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1;)pin",
    },
    PinCase{
        .name = "3.1-min",
        .compat = "3.1.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = true,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain
grouID: group
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.1.0
[tx]
gaslimit:3000000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:
isSerialExecute:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.1-pbft",
        .compat = "3.1.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.1.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.1-rpbft",
        .compat = "3.1.0",
        .consensusType = "rpbft",
        .withFeatures = true,
        .smCrypto = true,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:1
chainID: chain0
grouID: group0
[consensys]
consensus_type: rpbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.1.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
[features]
feature_balance:1
bugfix_revert:1
feature_sharding:0
feature_rpbft:1
bugfix_auth_check:1
feature_evm_cancun:1
feature_balance_policy2:1
feature_raw_address:1
feature_rpbft_term_weight:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.6-pbft",
        .compat = "3.6.0",
        .consensusType = "pbft",
        .withFeatures = true,
        .smCrypto = false,
        .authCheck = true,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.6.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:1
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:4
epochBlockNum:1000
[features]
feature_balance:1
bugfix_revert:1
feature_sharding:0
feature_rpbft:1
bugfix_auth_check:1
feature_evm_cancun:1
feature_balance_policy2:1
feature_raw_address:1
feature_rpbft_term_weight:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.6-rpbft",
        .compat = "3.6.0",
        .consensusType = "rpbft",
        .withFeatures = false,
        .smCrypto = true,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:1
chainID: chain0
grouID: group0
[consensys]
consensus_type: rpbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.6.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:2
epochBlockNum:500
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.17-pbft",
        .compat = "3.17.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 1,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.17.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:4
epochBlockNum:1000
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.17-rpbft",
        .compat = "3.17.0",
        .consensusType = "rpbft",
        .withFeatures = true,
        .smCrypto = true,
        .authCheck = true,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:1
chainID: chain0
grouID: group0
[consensys]
consensus_type: rpbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.17.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:1
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:2
epochBlockNum:500
[features]
feature_balance:1
bugfix_revert:1
feature_sharding:0
feature_rpbft:1
bugfix_auth_check:1
feature_evm_cancun:1
feature_balance_policy2:1
feature_raw_address:1
feature_rpbft_term_weight:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.18-pbft",
        .compat = "3.18.0",
        .consensusType = "pbft",
        .withFeatures = true,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 1,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.18.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:4
epochBlockNum:1000
[features]
feature_balance:1
bugfix_revert:1
feature_sharding:0
feature_rpbft:1
bugfix_auth_check:1
feature_evm_cancun:1
feature_balance_policy2:1
feature_raw_address:1
feature_rpbft_term_weight:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.18-rpbft",
        .compat = "3.18.0",
        .consensusType = "rpbft",
        .withFeatures = false,
        .smCrypto = true,
        .authCheck = true,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:1
chainID: chain0
grouID: group0
[consensys]
consensus_type: rpbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.18.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:1
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:2
epochBlockNum:500
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.18-pbft-all",
        .compat = "3.18.0",
        .consensusType = "pbft",
        .withFeatures = true,
        .smCrypto = true,
        .authCheck = true,
        .executorVersion = 1,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:1
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.18.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:1
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:4
epochBlockNum:1000
[features]
feature_balance:1
bugfix_revert:1
feature_sharding:0
feature_rpbft:1
bugfix_auth_check:1
feature_evm_cancun:1
feature_balance_policy2:1
feature_raw_address:1
feature_rpbft_term_weight:0
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
    PinCase{
        .name = "3.18-pbft-plain",
        .compat = "3.18.0",
        .consensusType = "pbft",
        .withFeatures = false,
        .smCrypto = false,
        .authCheck = false,
        .executorVersion = 0,
        .onlyCompatAndNodes = false,
        .golden = R"pin([chain]
sm_crypto:0
chainID: chain0
grouID: group0
[consensys]
consensus_type: pbft
block_tx_count_limit:1000
leader_period:1
[version]
compatibility_version:3.18.0
[tx]
gaslimit:300000000
[executor]
iswasm: 0
isAuthCheck:0
authAdminAccount:0x1111111111111111111111111111111111111111
isSerialExecute:1
epochSealerNum:4
epochBlockNum:1000
node.0:aaaa5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,3
node.1:bbbb5678901234567890123456789012345678901234567890123456789012341234567890123456789012345678901234567890123456789012345678901234,1
)pin",
    },
});
}  // namespace

BOOST_AUTO_TEST_CASE(v1GenesisPinIsByteIdentical)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    for (auto const& pinCase : c_cases)
    {
        BOOST_TEST_CONTEXT("case " << pinCase.name)
        {
            NodeConfig cfg(keyFactory);
            try
            {
                cfg.loadGenesisConfigFromString(v1Genesis(pinCase));
            }
            catch (std::exception const& e)
            {
                // Keep going so one regression reports every affected case, not just the first.
                BOOST_ERROR("genesis no longer loads: " << boost::diagnostic_information(e));
                continue;
            }
            BOOST_REQUIRE(cfg.ledgerConfig());
            auto const pin = generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
            BOOST_CHECK_EQUAL(pin, std::string(pinCase.golden));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
