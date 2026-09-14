/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5354: negative numeric config values must be rejected
 *        instead of wrapping to a huge size_t and passing the `<= 0` guard.
 */
#include "ExceptionCheck.h"
#include "NodeConfigLoaderProbe.h"
#include <bcos-tool/Exceptions.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigNegativeValuesTest)

BOOST_AUTO_TEST_CASE(txpoolLimitNegativeRejected)
{
    LoaderProbe a;
    BOOST_CHECK_EXCEPTION(a.loadTxPoolConfig(fromIni("[txpool]\nlimit=-1\n")),
        bcos::tool::InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "txpool.limit"); });

    LoaderProbe b;  // positive still accepted
    b.loadTxPoolConfig(fromIni("[txpool]\nlimit=7\n"));
    BOOST_CHECK_EQUAL(b.txpoolLimit(), 7U);
}

BOOST_AUTO_TEST_CASE(threadPoolCountsNegativeRejected)
{
    LoaderProbe a;
    BOOST_CHECK_EXCEPTION(a.loadOthersConfig(fromIni("[thread_pool]\nio_thread_count=-1\n")),
        bcos::tool::InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "io_thread_count"); });

    LoaderProbe b;
    BOOST_CHECK_EXCEPTION(b.loadOthersConfig(fromIni("[thread_pool]\nio_thread_count=0\n")),
        bcos::tool::InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "io_thread_count"); });

    LoaderProbe c;
    BOOST_CHECK_EXCEPTION(c.loadOthersConfig(fromIni("[thread_pool]\ntbb_thread_count=-1\n")),
        bcos::tool::InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "tbb_thread_count"); });

    LoaderProbe d;  // 0 means "auto" for tbb_thread_count and stays accepted
    d.loadOthersConfig(fromIni("[thread_pool]\nio_thread_count=3\ntbb_thread_count=0\n"));
    BOOST_CHECK_EQUAL(d.ioThreadCount(), 3U);
    BOOST_CHECK_EQUAL(d.tbbThreadCount(), 0U);
}

BOOST_AUTO_TEST_CASE(consensusCountsNegativeRejected)
{
    struct Case
    {
        const char* key;
        const char* needle;
    };
    for (auto const& c :
        {Case{"checkpoint_timeout", "checkpoint_timeout"}, Case{"pipeline_size", "pipeline_size"},
            Case{"pipeline_per_peer_capacity", "pipeline_per_peer_capacity"},
            Case{"pipeline_lru_capacity", "pipeline_lru_capacity"},
            Case{"pipeline_max_peers", "pipeline_max_peers"}})
    {
        LoaderProbe probe;
        auto ini = std::string("[consensus]\n") + c.key + "=-1\n";
        BOOST_CHECK_EXCEPTION(probe.loadConsensusConfig(fromIni(ini)), bcos::tool::InvalidConfig,
            [&](auto const& e) { return errinfoContains(e, c.needle); });
    }

    LoaderProbe ok;  // defaults still load
    BOOST_CHECK_NO_THROW(ok.loadConsensusConfig(fromIni("[consensus]\npipeline_size=50\n")));
    BOOST_CHECK_EQUAL(ok.pipelineSize(), 50U);
}

BOOST_AUTO_TEST_CASE(failoverLeaseTtlNegativeRejected)
{
    LoaderProbe a;
    BOOST_CHECK_EXCEPTION(
        a.loadFailOverConfig(
            fromIni("[failover]\nenable=true\nmember_id=m\nlease_ttl=-1\n"), false),
        bcos::tool::InvalidConfig, [](auto const& e) { return errinfoContains(e, "lease_ttl"); });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
