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

// The filter budgets are read as block-count bounds: a non-positive value makes the one-shot
// eth_getLogs cap or the streaming paging step reject/return nothing, so both are validated at
// boot. Two separate keys: filter_max_process_block is the streaming paging step (small) and
// filter_max_block_range is the one-shot range cap (query-sized). Pinning both also pins the
// shipped defaults, which are an operational contract: 10 for the step, 10000 for the cap.
BOOST_AUTO_TEST_CASE(filterBudgetsRejectNonPositiveAndShipExpectedDefaults)
{
    struct Case
    {
        const char* section;
        bool web3;
        const char* key;
    };
    for (auto const& c : {Case{"rpc", false, "filter_max_process_block"},
             Case{"rpc", false, "filter_max_block_range"},
             Case{"web3_rpc", true, "filter_max_process_block"},
             Case{"web3_rpc", true, "filter_max_block_range"}})
    {
        for (auto const& bad : {"0", "-1"})
        {
            LoaderProbe probe;
            auto const ini = std::string("[") + c.section + "]\n" + c.key + "=" + bad + "\n";
            // The message must name the section and the field, so an operator can find the line.
            // Its shape is "[<section>].<field> must be positive, got <value>".
            auto const needle = std::string("[") + c.section + "]." + c.key;
            if (c.web3)
            {
                BOOST_CHECK_EXCEPTION(probe.loadWeb3RpcConfig(fromIni(ini)),
                    bcos::tool::InvalidConfig,
                    [&](auto const& e) { return errinfoContains(e, needle); });
            }
            else
            {
                BOOST_CHECK_EXCEPTION(probe.loadRpcConfig(fromIni(ini)), bcos::tool::InvalidConfig,
                    [&](auto const& e) { return errinfoContains(e, needle); });
            }
        }
    }

    // An omitted key resolves to the shipped default for both sections.
    LoaderProbe defaults;
    defaults.loadRpcConfig(fromIni("[rpc]\nfilter_timeout=300\n"));
    BOOST_CHECK_EQUAL(defaults.rpcMaxProcessBlock(), 10U);
    BOOST_CHECK_EQUAL(defaults.rpcMaxBlockRange(), 10000U);

    LoaderProbe web3Defaults;
    web3Defaults.loadWeb3RpcConfig(fromIni("[web3_rpc]\nfilter_timeout=300\n"));
    BOOST_CHECK_EQUAL(web3Defaults.web3MaxProcessBlock(), 10U);
    BOOST_CHECK_EQUAL(web3Defaults.web3MaxBlockRange(), 10000U);

    // The two keys are independent: overriding one leaves the other at its default.
    LoaderProbe split;
    split.loadRpcConfig(fromIni("[rpc]\nfilter_max_block_range=500\n"));
    BOOST_CHECK_EQUAL(split.rpcMaxProcessBlock(), 10U);
    BOOST_CHECK_EQUAL(split.rpcMaxBlockRange(), 500U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
