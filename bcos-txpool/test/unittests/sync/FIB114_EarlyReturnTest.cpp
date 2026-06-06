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
 * @file FIB114_EarlyReturnTest.cpp
 * @brief Regression test for FIB-114: TransactionSync::requestMissedTxs short-circuits
 *        with (nullptr, true) when the missed-tx list is empty, instead of dispatching
 *        an empty list through the ledger-fetch / peer-fetch chain.
 */

#include "test/unittests/txpool/TxPoolFixture.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB114_EarlyReturn, TxPoolFixture)

BOOST_AUTO_TEST_CASE(emptyMissedTxsShortCircuitsSynchronously)
{
    auto sync = this->sync();
    BOOST_REQUIRE(sync);

    auto empty = std::make_shared<bcos::crypto::HashList>();

    bool fired = false;
    bcos::Error::Ptr err;
    bool ok = false;

    // The fix routes empty input straight to the callback BEFORE returning from
    // requestMissedTxs, so by the time the call returns the callback must already have
    // fired with (nullptr, true). Pre-fix the call dispatched async into the ledger
    // and the callback would not be set yet at this synchronisation point.
    sync->requestMissedTxs(/*generatedNodeID=*/nullptr, empty, /*verifiedProposal=*/nullptr,
        [&](bcos::Error::Ptr e, bool result) {
            err = std::move(e);
            ok = result;
            fired = true;
        });

    BOOST_CHECK(fired);
    BOOST_CHECK(!err);
    BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(nullMissedTxsAlsoShortCircuits)
{
    auto sync = this->sync();
    BOOST_REQUIRE(sync);

    bool fired = false;
    sync->requestMissedTxs(
        nullptr, /*missedTxs=*/nullptr, nullptr, [&](bcos::Error::Ptr, bool result) {
            BOOST_CHECK(result);
            fired = true;
        });

    BOOST_CHECK(fired);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
