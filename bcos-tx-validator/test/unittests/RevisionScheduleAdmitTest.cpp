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
 * @file RevisionScheduleAdmitTest.cpp
 * @brief The L1 EL lane records no evmc_revision row: admission takes the revision from the
 *        bound ledger::EvmcRevisionSchedule at the head block's timestamp.
 */

#include "AdmissionHarness.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txvalidator;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RevisionScheduleAdmitTest)

namespace
{
/// The harness snapshot with no revision of its own, as getLedgerConfig builds it for a v2
/// chain (no evmc_revision row), with the head block at @p headTimestampMs.
void dropSnapshotRevision(AdmitHarness& harness, int64_t headTimestampMs)
{
    auto config = std::make_shared<ledger::LedgerConfig>();
    config->setChainId(*harness.ledgerConfig->chainId());
    config->setGasPrice(harness.ledgerConfig->gasPrice());
    config->setBlockNumber(harness.ledgerConfig->blockNumber());
    config->setGasLimit(harness.ledgerConfig->gasLimit());
    config->setTimestamp(headTimestampMs);
    harness.ledgerConfig = std::move(config);
}
}  // namespace

// The schedule's answer is what the revision-dependent checks judge by: a pre-Berlin revision
// refuses the typed transaction, a Prague one admits it. The schedule is asked about the head's
// timestamp in seconds (the snapshot carries milliseconds) with PoS difficulty 0.
BOOST_AUTO_TEST_CASE(revisionComesFromTheScheduleAtTheHeadTimestamp)
{
    for (auto const [revision, expected] :
        {std::pair{EVMC_ISTANBUL, TransactionStatus::TxTypeNotSupported},
            std::pair{EVMC_PRAGUE, TransactionStatus::None}})
    {
        AdmitHarness harness;
        dropSnapshotRevision(harness, 1'700'000'123'000);
        std::vector<std::pair<int64_t, u256>> asked;
        harness.revisionSchedule = [&asked, revision](int64_t seconds, u256 const& difficulty) {
            asked.emplace_back(seconds, difficulty);
            return revision;
        };
        auto tx = admitTx();
        BOOST_CHECK(harness.run(*tx) == expected);
        BOOST_REQUIRE_EQUAL(asked.size(), 1U);
        BOOST_CHECK_EQUAL(asked[0].first, 1'700'000'123);
        BOOST_CHECK_EQUAL(asked[0].second, u256(0));
    }
}

// Without the schedule the same snapshot declares no revision, and the revision-dependent
// checks stand down: the typed transaction an Istanbul schedule refuses is admitted.
BOOST_AUTO_TEST_CASE(withoutAScheduleTheRevisionChecksStandDown)
{
    AdmitHarness harness;
    dropSnapshotRevision(harness, 1'700'000'123'000);
    auto tx = admitTx();
    BOOST_CHECK(harness.run(*tx) == TransactionStatus::None);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
