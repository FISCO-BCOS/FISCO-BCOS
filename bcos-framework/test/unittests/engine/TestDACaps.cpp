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
 * @file TestDACaps.cpp
 * @brief DACaps txFits / Budget semantics: 0 = uncapped, >0 = inclusive cap, forced-bytes
 * preload, and wrap-safe admission.
 */

#include "bcos-framework/engine/DACaps.h"
#include <boost/test/unit_test.hpp>
#include <limits>

using namespace bcos::engine;

BOOST_AUTO_TEST_SUITE(TestDACaps)

BOOST_AUTO_TEST_CASE(ZeroCapsAreUncapped)
{
    DACaps caps;
    // Default-constructed caps are 0 = uncapped for both gates.
    BOOST_CHECK(caps.txFits(std::numeric_limits<std::uint64_t>::max()));
    DACaps::Budget budget(caps, 0);
    BOOST_CHECK(budget.admits(std::numeric_limits<std::uint64_t>::max()));
    // Uncapped + forcedBytes>0: forced envelopes preload the budget, but with no cap
    // nothing is ever rejected (the cap==0 early return covers the forced path too).
    DACaps::Budget forcedBudget(caps, 100);
    BOOST_CHECK(forcedBudget.admits(std::numeric_limits<std::uint64_t>::max()));
}

BOOST_AUTO_TEST_CASE(TxFitsInclusiveCap)
{
    DACaps caps;
    caps.maxTxSize.store(100);
    BOOST_CHECK(caps.txFits(100));  // inclusive
    BOOST_CHECK(caps.txFits(99));
    BOOST_CHECK(!caps.txFits(101));
}

BOOST_AUTO_TEST_CASE(BudgetForcedBytesAndInclusiveAdmission)
{
    DACaps caps;
    caps.maxBlockSize.store(100);
    // Forced (undroppable) envelopes preload the budget.
    DACaps::Budget budget(caps, 40);
    BOOST_CHECK(budget.admits(60));  // exactly at cap -> admitted
    BOOST_CHECK(!budget.admits(1));  // over the cap -> rejected
    DACaps::Budget budget2(caps, 100);
    BOOST_CHECK(!budget2.admits(1));  // forced bytes alone at the cap
    // forced > cap: cap < m_used must reject even envelopeSize 0 (wrap-safe first conjunct).
    DACaps::Budget over(caps, 101);
    BOOST_CHECK(!over.admits(1));
    BOOST_CHECK(!over.admits(0));
}

BOOST_AUTO_TEST_CASE(BudgetAdmissionIsWrapSafe)
{
    DACaps caps;
    caps.maxBlockSize.store(std::numeric_limits<std::uint64_t>::max());
    DACaps::Budget budget(caps, std::numeric_limits<std::uint64_t>::max() - 1);
    // m_used + envelopeSize would wrap; the cap - m_used form must still admit exactly one.
    BOOST_CHECK(budget.admits(1));
    BOOST_CHECK(!budget.admits(1));
}

BOOST_AUTO_TEST_SUITE_END()
