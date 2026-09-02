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
 * @file test_GenesisFeatureFlagsCommit.cpp
 * @brief The SystemConfig feature_flags slot must arrive IN the genesis
 *        allocs (so the state root commits it) and Ledger only VERIFIES it —
 *        missing or mismatching slots refuse to build genesis.
 */
#include "GenesisFeatureFlagsHelper.h"
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/test/unittests/ExceptionCheck.h"
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
GenesisConfig makeConfigWithoutFlagsSlot()
{
    GenesisConfig genesisConfig;
    genesisConfig.m_txGasLimit = 3000000000;
    genesisConfig.m_compatibilityVersion =
        static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
    genesisConfig.m_features.push_back(FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
    genesisConfig.m_chainID = "901";
    genesisConfig.m_groupID = "group0";
    genesisConfig.m_allocs.push_back(Alloc{.address = "43000000000000000000000000000000000000c0",
        .balance = u256(0),
        .nonce = "0",
        .code = "6080604052",
        .storage = {}});
    return genesisConfig;
}

struct FeatureFlagsCommitFixture
{
    FeatureFlagsCommitFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    LedgerConfig makeParam()
    {
        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);
        return param;
    }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisFeatureFlagsCommitTest, FeatureFlagsCommitFixture)

BOOST_AUTO_TEST_CASE(FlagsSlotIsCommittedToStateRoot)
{
    task::syncWait([this]() -> task::Task<void> {
        // The whole point of moving the slot into the allocs: the genesis
        // state root over allocs WITH the slot differs from the root over
        // allocs WITHOUT it — i.e. the root now commits the feature set.
        auto without = makeConfigWithoutFlagsSlot();
        auto with = makeConfigWithoutFlagsSlot();
        appendGenesisFeatureFlagsSlot(with);
        auto rootWithout = co_await ledger::computeGenesisStateRoot(without);
        auto rootWith = co_await ledger::computeGenesisStateRoot(with);
        BOOST_CHECK_NE(rootWith.hex(), rootWithout.hex());

        // And a build with the slot present succeeds; B0 carries the
        // slot-inclusive root.
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, with, param));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        BOOST_CHECK_EQUAL(block->blockHeader()->stateRoot().hex(), rootWith.hex());
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(MissingFlagsSlotRefusesToBuild)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto genesisConfig = makeConfigWithoutFlagsSlot();  // slot absent
        BOOST_CHECK_EXCEPTION(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param),
            bcos::tool::InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "must carry the SystemConfig"); });
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(MismatchingFlagsSlotRefusesToBuild)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto genesisConfig = makeConfigWithoutFlagsSlot();
        appendGenesisFeatureFlagsSlot(genesisConfig);
        // Corrupt the packed value: the alloc now claims a different feature
        // set than the node runs with -> fail-fast.
        auto& slotValue = genesisConfig.m_allocs[0].storage.back().second;
        slotValue[slotValue.size() - 1] = (slotValue.back() == '0') ? '1' : '0';
        BOOST_CHECK_EXCEPTION(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return errinfoContains(
                    e, "feature_flags slot in the genesis allocs does not match");
            });
        co_return;
    }());
}

// The feature_flags gate runs on the FIRST-INIT path only. On restart the
// pinned stateRoot / genesis-data comparisons are the guards: an unchanged
// config restarts fine, and a config that perturbs the purely-computed
// expected feature set (here an extra enabled feature — feature_op_jovian is
// set by no genesis-default path) is refused by the genesis-pin guard, NOT by
// the feature-flags message. The latter would tell the operator to
// "regenerate the allocs", which is impossible for an initialized chain
// (B0's stateRoot pins them); it must also never fire just because a newer
// binary computes a different expected set.
BOOST_AUTO_TEST_CASE(RestartGuardsOnGenesisPinNotFeatureFlags)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        auto param = makeParam();
        auto genesisConfig = makeConfigWithoutFlagsSlot();
        appendGenesisFeatureFlagsSlot(genesisConfig);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));

        // Unchanged config: plain restart succeeds.
        BOOST_CHECK(co_await ledger::buildGenesisBlock(*ledger, genesisConfig, param));

        // Perturbed expected feature set: refused by the pin comparison.
        auto perturbed = genesisConfig;
        perturbed.m_features.push_back(FeatureSet{Features::Flag::feature_op_jovian, 1});
        BOOST_CHECK_EXCEPTION(co_await ledger::buildGenesisBlock(*ledger, perturbed, param),
            bcos::tool::InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "Genesis Data is inconsistent");
            });
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
