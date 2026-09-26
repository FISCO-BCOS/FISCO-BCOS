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
 * @file OpForkScheduleTest.cpp
 * @brief Pins bcos::ledger::resolveOpFork — the single OP fork-activation parser the
 *        executor (configAt) and the devp2p header validator both delegate to:
 *        isthmus-unset zero-start baseline, unscheduled-rung implication, UINT64_MAX =
 *        not scheduled, ts >= forkTime activation.
 * @date 2026/9/22
 */
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <boost/test/unit_test.hpp>
#include <limits>

// Boost.Test failure messages need a printer for the enum.
namespace boost::test_tools::tt_detail
{
template <>
struct print_log_value<bcos::ledger::OpFork>
{
    void operator()(std::ostream& ostr, bcos::ledger::OpFork value)
    {
        ostr << static_cast<int>(value);
    }
};
}  // namespace boost::test_tools::tt_detail

using bcos::ledger::OpFork;
using bcos::ledger::OpForkSchedule;
using bcos::ledger::resolveOpFork;

namespace
{
constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();

/// Full Bedrock..Karst ladder, 100 seconds per rung: regolith=100, canyon=200, ...,
/// karst=1000. isthmus_time is SET, which is what turns the pre-Isthmus rungs live.
OpForkSchedule fullSched()
{
    OpForkSchedule s;
    s.m_regolithTime = 100;
    s.m_canyonTime = 200;
    s.m_deltaTime = 300;
    s.m_ecotoneTime = 400;
    s.m_fjordTime = 500;
    s.m_graniteTime = 600;
    s.m_holoceneTime = 700;
    s.m_isthmusTime = 800;
    s.m_jovianTime = 900;
    s.m_karstTime = 1000;
    return s;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpForkScheduleResolveSuite)

// An unset isthmus_time means "Isthmus is the zero-start baseline": every timestamp
// below jovian_time resolves to Isthmus and the pre-Isthmus rungs are never consulted.
// This is the only shape existing chains configure (jovian_time/karst_time at most).
BOOST_AUTO_TEST_CASE(unsetIsthmusIsTheZeroStartBaseline)
{
    OpForkSchedule s;
    s.m_jovianTime = 1000;
    s.m_karstTime = 2000;
    BOOST_CHECK(resolveOpFork(s, 0) == OpFork::Isthmus);
    BOOST_CHECK(resolveOpFork(s, 999) == OpFork::Isthmus);
    BOOST_CHECK(resolveOpFork(s, 1000) == OpFork::Jovian);
    BOOST_CHECK(resolveOpFork(s, 1999) == OpFork::Jovian);
    BOOST_CHECK(resolveOpFork(s, 2000) == OpFork::Karst);
    BOOST_CHECK(resolveOpFork(s, kNever - 1) == OpFork::Karst);

    // Nothing scheduled at all: still the Isthmus baseline, for every timestamp.
    OpForkSchedule empty;
    BOOST_CHECK(resolveOpFork(empty, 0) == OpFork::Isthmus);
    BOOST_CHECK(resolveOpFork(empty, kNever - 1) == OpFork::Isthmus);

    // jovian/karst active from genesis (the [op_fork_timestamps] jovian_time=0
    // karst_time=0 shape): Karst everywhere.
    OpForkSchedule fromGenesis;
    fromGenesis.m_jovianTime = 0;
    fromGenesis.m_karstTime = 0;
    BOOST_CHECK(resolveOpFork(fromGenesis, 0) == OpFork::Karst);
    BOOST_CHECK(resolveOpFork(fromGenesis, kNever - 1) == OpFork::Karst);
}

// The full ladder with isthmus_time set: every rung at its exact boundary second
// (op-node's IsX(ts) is `ts >= *Time`), Bedrock as the fallback below the earliest
// scheduled fork.
BOOST_AUTO_TEST_CASE(fullLadderResolvesEveryRung)
{
    const auto s = fullSched();
    BOOST_CHECK(resolveOpFork(s, 0) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(s, 99) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(s, 100) == OpFork::Regolith);
    BOOST_CHECK(resolveOpFork(s, 200) == OpFork::Canyon);
    BOOST_CHECK(resolveOpFork(s, 300) == OpFork::Delta);
    BOOST_CHECK(resolveOpFork(s, 400) == OpFork::Ecotone);
    BOOST_CHECK(resolveOpFork(s, 500) == OpFork::Fjord);
    BOOST_CHECK(resolveOpFork(s, 600) == OpFork::Granite);
    BOOST_CHECK(resolveOpFork(s, 700) == OpFork::Holocene);
    BOOST_CHECK(resolveOpFork(s, 800) == OpFork::Isthmus);
    BOOST_CHECK(resolveOpFork(s, 900) == OpFork::Jovian);
    BOOST_CHECK(resolveOpFork(s, 1000) == OpFork::Karst);
    BOOST_CHECK(resolveOpFork(s, 1001) == OpFork::Karst);
}

// An unscheduled intermediate rung is IMPLIED by a later scheduled fork — skipped,
// never "inactive": isthmus_time set with canyon/ecotone/holocene unset still resolves
// ts >= T to Isthmus, and a ladder with only canyon/fjord/isthmus set runs Canyon
// straight through to Fjord.
BOOST_AUTO_TEST_CASE(unscheduledRungsAreImpliedNotInactive)
{
    OpForkSchedule isthmusOnly;
    isthmusOnly.m_isthmusTime = 500;
    BOOST_CHECK(resolveOpFork(isthmusOnly, 0) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(isthmusOnly, 499) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(isthmusOnly, 500) == OpFork::Isthmus);
    BOOST_CHECK(resolveOpFork(isthmusOnly, kNever - 1) == OpFork::Isthmus);

    OpForkSchedule sparse;
    sparse.m_canyonTime = 200;
    sparse.m_fjordTime = 500;
    sparse.m_isthmusTime = 800;
    BOOST_CHECK(resolveOpFork(sparse, 0) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(sparse, 199) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(sparse, 200) == OpFork::Canyon);
    BOOST_CHECK(resolveOpFork(sparse, 499) == OpFork::Canyon);
    BOOST_CHECK(resolveOpFork(sparse, 500) == OpFork::Fjord);
    BOOST_CHECK(resolveOpFork(sparse, 799) == OpFork::Fjord);
    BOOST_CHECK(resolveOpFork(sparse, 800) == OpFork::Isthmus);

    // Two forks activating at the same second run the later one (op-node's own IsX
    // ordering); a later scheduled rung never downgrades to an earlier one.
    OpForkSchedule sameSecond;
    sameSecond.m_isthmusTime = 100;
    sameSecond.m_jovianTime = 100;
    sameSecond.m_karstTime = 100;
    BOOST_CHECK(resolveOpFork(sameSecond, 99) == OpFork::Bedrock);
    BOOST_CHECK(resolveOpFork(sameSecond, 100) == OpFork::Karst);
}

// The enum order IS the comparison semantics the header validator and executor rely on
// (`fork >= OpFork::Canyon`); pin the ordinals so an accidental reorder fails loudly.
BOOST_AUTO_TEST_CASE(forkEnumOrderIsTheActivationOrder)
{
    BOOST_CHECK(OpFork::Bedrock < OpFork::Regolith);
    BOOST_CHECK(OpFork::Regolith < OpFork::Canyon);
    BOOST_CHECK(OpFork::Canyon < OpFork::Delta);
    BOOST_CHECK(OpFork::Delta < OpFork::Ecotone);
    BOOST_CHECK(OpFork::Ecotone < OpFork::Fjord);
    BOOST_CHECK(OpFork::Fjord < OpFork::Granite);
    BOOST_CHECK(OpFork::Granite < OpFork::Holocene);
    BOOST_CHECK(OpFork::Holocene < OpFork::Isthmus);
    BOOST_CHECK(OpFork::Isthmus < OpFork::Jovian);
    BOOST_CHECK(OpFork::Jovian < OpFork::Karst);
}

BOOST_AUTO_TEST_SUITE_END()
