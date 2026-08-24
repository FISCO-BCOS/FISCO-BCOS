/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-R*: BlockContext::setVMSchedule vs ledger features.
 *  @file CompatBlockContextTest.cpp
 */

#include "../../mock/MockLedger.h"
#include "CompatTestFixture.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Wait.h"
#include "executive/BlockContext.h"

#include <boost/test/unit_test.hpp>
namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatBlockContext)

BOOST_AUTO_TEST_CASE(FC_R_block_context_schedule_ladder)
{
    using namespace bcos::executor;
    using compat::CompatFeatureProfile;

    auto hashImpl = std::make_shared<crypto::Keccak256>();
    auto backend = std::make_shared<storage::StateStorage>(nullptr, false);
    auto stateStorage = std::make_shared<storage::StateStorage>(backend, false);
    auto ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());

    auto makeContext = [&](ledger::Features const& features, uint32_t blockVersion) {
        task::syncWait(ledger::writeToStorage(features, *stateStorage, 1));
        auto ctx = std::make_shared<BlockContext>(
            stateStorage, ledgerCache, hashImpl, 1, h256(), 0, blockVersion, false, backend);
        ctx->setFeatures(features);
        ctx->setVMSchedule();
        return ctx;
    };

    {
        auto ctx = makeContext(CompatFeatureProfile::legacyLondon(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_0_VERSION));
        BOOST_CHECK_EQUAL(ctx->vmSchedule().enableCanCun, false);
        BOOST_CHECK_EQUAL(ctx->vmSchedule().enablePairs, false);
    }
    {
        auto ctx = makeContext(CompatFeatureProfile::legacyLondon(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
        BOOST_CHECK(ctx->vmSchedule().enablePairs);
        BOOST_CHECK(!ctx->vmSchedule().enableCanCun);
    }
    {
        auto ctx = makeContext(CompatFeatureProfile::cancunOnly(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION));
        BOOST_CHECK(ctx->vmSchedule().enableCanCun);
        BOOST_CHECK(!ctx->vmSchedule().enablePrague);
    }
    {
        auto ctx = makeContext(CompatFeatureProfile::pragueEnabled(),
            static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        BOOST_CHECK(ctx->vmSchedule().enablePrague);
        BOOST_CHECK(!ctx->vmSchedule().enableOsaka);
    }
}

BOOST_AUTO_TEST_SUITE_END()  // CompatBlockContext
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
