/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// [executor] eest_replay_mode: the switch that selects the EESTReplay admission column, and the
// loader. The startup gate (executor.version=2 only) is NodeConfigBlockDriverTest.

#include "NodeConfigLoaderProbe.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigEestReplayTest)

BOOST_AUTO_TEST_CASE(offByDefault)
{
    LoaderProbe probe;
    probe.loadOthersConfig({});
    BOOST_CHECK(!probe.eestReplayMode());
}

// The loader only parses the key: the lane gate (executor.version=2 only) needs
// config.genesis and runs in validateELModeInvariants (NodeConfigBlockDriverTest).
BOOST_AUTO_TEST_CASE(parsedByTheLoader)
{
    LoaderProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadOthersConfig(fromIni("[executor]\neest_replay_mode=true\n")));
    BOOST_CHECK(probe.eestReplayMode());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
