/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Deprecated suite name — cases migrated to --run_test=Compat.
 *  @file ForwardCompatibilityTest.cpp
 */

#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(ForwardCompatibilityTest)

BOOST_AUTO_TEST_CASE(migrated_to_compat_suite)
{
    BOOST_TEST_MESSAGE(
        "Revision/modexp/feature-default/static-guard tests moved to "
        "bcos-executor/test/unittest/evmone/compat/ — run: "
        "--run_test=Compat");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
