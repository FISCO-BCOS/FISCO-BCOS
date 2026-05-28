/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-security/cloudkms/utils.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::security;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(CloudKmsUtilsTest)

BOOST_AUTO_TEST_CASE(splitOnColonProducesParts)
{
    std::vector<std::string> out;
    BOOST_REQUIRE(splitKmsUrl("aws:us-east-1:my-key", out));
    BOOST_REQUIRE_EQUAL(out.size(), 3U);
    BOOST_CHECK_EQUAL(out[0], "aws");
    BOOST_CHECK_EQUAL(out[1], "us-east-1");
    BOOST_CHECK_EQUAL(out[2], "my-key");
}

BOOST_AUTO_TEST_CASE(splitEmptyReturnsEmpty)
{
    std::vector<std::string> out;
    BOOST_REQUIRE(splitKmsUrl("", out));
    BOOST_CHECK(out.empty());
}

BOOST_AUTO_TEST_CASE(splitWithoutColonReturnsSingle)
{
    std::vector<std::string> out;
    BOOST_REQUIRE(splitKmsUrl("standalone", out));
    BOOST_REQUIRE_EQUAL(out.size(), 1U);
    BOOST_CHECK_EQUAL(out[0], "standalone");
}

BOOST_AUTO_TEST_CASE(splitClearsExistingContent)
{
    std::vector<std::string> out{"pre-existing", "garbage"};
    BOOST_REQUIRE(splitKmsUrl("only", out));
    BOOST_REQUIRE_EQUAL(out.size(), 1U);
    BOOST_CHECK_EQUAL(out[0], "only");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
