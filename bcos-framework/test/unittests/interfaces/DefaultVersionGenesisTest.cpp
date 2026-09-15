/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-framework/ledger/Features.h"
#include "protocol/Protocol.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::ledger;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(DefaultVersionGenesisTest)

// Regression for issue #5586: a release that registers a flag at a new BlockVersion must
// move DEFAULT_VERSION with it, or a chain created with default settings never activates
// the fix. Pins DEFAULT_VERSION to the newest data version and to the 3.17.1 flag.
BOOST_AUTO_TEST_CASE(defaultVersionActivatesLatestFlags)
{
    BOOST_CHECK_EQUAL(static_cast<uint32_t>(bcos::protocol::DEFAULT_VERSION),
        static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));

    Features features;
    features.setGenesisFeatures(bcos::protocol::DEFAULT_VERSION);
    BOOST_CHECK(features.get(Features::Flag::bugfix_v1_eoa_as_contract));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
