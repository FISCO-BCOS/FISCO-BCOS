// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file L1MethodWindowTest.cpp
// @brief The one fork-window table behind the L1 Engine methods' -38005 answers
// (engine_common::l1MethodInWindow), pinned fork by fork against execution-apis
// (shanghai/cancun/prague/osaka/amsterdam.md "Update the methods of previous forks") and
// geth's checkFork sets.
#include "bcos-engine/EngineServiceCommon.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;
using engine_common::L1Method;
using engine_common::l1MethodInWindow;
using engine_common::requireL1MethodWindow;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(L1MethodWindowTest)

namespace
{
constexpr std::array<evmc_revision, 5> c_revisions{
    EVMC_PARIS, EVMC_SHANGHAI, EVMC_CANCUN, EVMC_PRAGUE, EVMC_OSAKA};

/// The versions of @p method in window at @p revision, as a bitmask (bit v = version v).
unsigned windowMask(L1Method method, evmc_revision revision)
{
    unsigned mask = 0;
    for (std::uint32_t version = 1; version <= 5; ++version)
    {
        if (l1MethodInWindow(method, version, revision))
        {
            mask |= 1U << version;
        }
    }
    return mask;
}
constexpr unsigned v(std::initializer_list<unsigned> versions)
{
    unsigned mask = 0;
    for (auto version : versions)
    {
        mask |= 1U << version;
    }
    return mask;
}
}  // namespace

BOOST_AUTO_TEST_CASE(newPayloadWindows)
{
    // V1 has no window (shanghai.md adds no rule); V2 up to Shanghai; V3 Cancun only
    // (prague.md); V4 Prague and Osaka (osaka.md adds no newPayloadV5; BPO rides Osaka);
    // V5 is Amsterdam, never in window here.
    BOOST_CHECK_EQUAL(windowMask(L1Method::NewPayload, EVMC_PARIS), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::NewPayload, EVMC_SHANGHAI), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::NewPayload, EVMC_CANCUN), v({1, 3}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::NewPayload, EVMC_PRAGUE), v({1, 4}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::NewPayload, EVMC_OSAKA), v({1, 4}));
}

BOOST_AUTO_TEST_CASE(getPayloadWindows)
{
    // Each getPayload version serves exactly one fork from Cancun on (cancun/prague/osaka.md
    // retire the previous version at each fork); V6 is Amsterdam.
    BOOST_CHECK_EQUAL(windowMask(L1Method::GetPayload, EVMC_PARIS), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::GetPayload, EVMC_SHANGHAI), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::GetPayload, EVMC_CANCUN), v({1, 3}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::GetPayload, EVMC_PRAGUE), v({1, 4}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::GetPayload, EVMC_OSAKA), v({1, 5}));
}

BOOST_AUTO_TEST_CASE(forkchoiceUpdatedWindows)
{
    // V1/V2 Paris and Shanghai (cancun.md retires V2 with -38005; V1 has no spec rule and
    // geth answers -32602 past Shanghai, this table -38005); V3 Cancun through
    // Osaka — osaka.md adds no fcuV4, so V3 is the method that builds Osaka blocks (geth
    // checkFork Cancun, Prague, Osaka, BPO1, BPO2); V4 is Amsterdam.
    BOOST_CHECK_EQUAL(windowMask(L1Method::ForkchoiceUpdated, EVMC_PARIS), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::ForkchoiceUpdated, EVMC_SHANGHAI), v({1, 2}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::ForkchoiceUpdated, EVMC_CANCUN), v({3}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::ForkchoiceUpdated, EVMC_PRAGUE), v({3}));
    BOOST_CHECK_EQUAL(windowMask(L1Method::ForkchoiceUpdated, EVMC_OSAKA), v({3}));
}

BOOST_AUTO_TEST_CASE(requireThrowsUnsupportedForkNamingTheMethod)
{
    for (auto revision : c_revisions)
    {
        BOOST_CHECK_NO_THROW(requireL1MethodWindow(L1Method::NewPayload, 1, revision));
    }
    auto const names = [](L1Method method, std::uint32_t version, evmc_revision revision,
                           std::string_view needle) {
        try
        {
            requireL1MethodWindow(method, version, revision);
            BOOST_FAIL("expected UnsupportedFork");
        }
        catch (UnsupportedFork const& e)
        {
            std::string const what = boost::diagnostic_information(e);
            BOOST_CHECK_MESSAGE(what.find(needle) != std::string::npos, what);
        }
    };
    names(L1Method::NewPayload, 2, EVMC_CANCUN, "newPayloadV2");
    names(L1Method::GetPayload, 4, EVMC_OSAKA, "getPayloadV4: the payload was built");
    names(L1Method::ForkchoiceUpdated, 2, EVMC_CANCUN, "forkchoiceUpdatedV2");
    names(L1Method::ForkchoiceUpdated, 5, EVMC_OSAKA, "forkchoiceUpdatedV5");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
