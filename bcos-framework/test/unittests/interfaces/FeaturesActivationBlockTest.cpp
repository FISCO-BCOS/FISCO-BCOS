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
 * @file FeaturesActivationBlockTest.cpp
 * @brief Features activation-block data path + feature_mpt_state_root flag (spec §4.3, §5.6)
 */
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/FeaturesStorage.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
// Entry::setObject/getObject serialize through boost archives; Entry.h only pulls the
// basic_archive types, the concrete archives are the consumer's include.
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/test/unit_test.hpp>
#include <magic_enum/magic_enum.hpp>

using namespace bcos;
using namespace bcos::ledger;

namespace
{
using ConfigStorage = storage2::memory_storage::MemoryStorage<executor_v1::StateKey,
    executor_v1::StateValue, storage2::memory_storage::ORDERED>;

task::Task<void> writeFlagAt(
    ConfigStorage& storage, std::string_view flagName, protocol::BlockNumber enableNumber)
{
    storage::Entry entry;
    entry.setObject(SystemConfigEntry{std::string{"1"}, enableNumber});
    co_await storage2::writeOne(
        storage, executor_v1::StateKey(SYS_CONFIG, flagName), std::move(entry));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FeaturesActivationBlockSuite)

BOOST_AUTO_TEST_CASE(MptStateRootFlagExistsWithPermanentValue)
{
    BOOST_CHECK(Features::contains("feature_mpt_state_root"));
    // The explicit enum value is persisted on-chain via feature_flags (Features.h "PERMANENT"
    // rule) — pin it so a reorder/renumber cannot slip through review.
    BOOST_CHECK_EQUAL(magic_enum::enum_integer(Features::Flag::feature_mpt_state_root), 58);
}

BOOST_AUTO_TEST_CASE(ActivationBlockReturnsMinusOneWhenUnset)
{
    Features features;
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_mpt_state_root), -1);
}

BOOST_AUTO_TEST_CASE(MemberReadFromStorageRecordsEnableNumber)
{
    ConfigStorage storage;
    task::syncWait(writeFlagAt(storage, "feature_mpt_state_root", 100));

    Features features;
    task::syncWait(features.readFromStorage(storage, /*blockNumber*/ 200));

    BOOST_CHECK(features.get(Features::Flag::feature_mpt_state_root));
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_mpt_state_root), 100);
}

BOOST_AUTO_TEST_CASE(FreeFunctionReadFromStorageRecordsEnableNumber)
{
    ConfigStorage storage;
    task::syncWait(writeFlagAt(storage, "feature_mpt_state_root", 77));

    Features features;
    task::syncWait(readFromStorage(features, storage, /*blockNumber*/ 200));

    BOOST_CHECK(features.get(Features::Flag::feature_mpt_state_root));
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_mpt_state_root), 77);
}

BOOST_AUTO_TEST_CASE(ActivationBlockNotRecordedBelowEnableNumber)
{
    ConfigStorage storage;
    task::syncWait(writeFlagAt(storage, "feature_mpt_state_root", 100));

    Features features;
    task::syncWait(features.readFromStorage(storage, /*blockNumber*/ 50));

    // blockNumber < enableNumber: the flag is not enabled, and no activation block either.
    BOOST_CHECK(!features.get(Features::Flag::feature_mpt_state_root));
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_mpt_state_root), -1);
}

BOOST_AUTO_TEST_CASE(BareSetStillReportsMinusOne)
{
    Features features;
    features.set(Features::Flag::feature_mpt_state_root);

    BOOST_CHECK(features.get(Features::Flag::feature_mpt_state_root));
    // set() carries no enableNumber context — by convention activationBlockOf stays -1;
    // callers (shouldBuildMPT, PR-17) must check get(flag) before trusting the number.
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_mpt_state_root), -1);
}

BOOST_AUTO_TEST_SUITE_END()
