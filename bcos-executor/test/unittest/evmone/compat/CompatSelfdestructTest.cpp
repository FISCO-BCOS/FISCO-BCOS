/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-SD*: selfdestruct / EIP-6780 documented deviation (S0 D3).
 *  @file CompatSelfdestructTest.cpp
 */

#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatSelfdestruct)

BOOST_AUTO_TEST_CASE(FC_SD_eip6780_deviation_documented)
{
    BOOST_TEST_MESSAGE(
        "S0-D3: host selfdestruct returns true; full EIP-6780 same-tx-create tracking not "
        "implemented — Cancun+ may differ from mainnet on pre-existing contract SELFDESTRUCT");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatSelfdestruct
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
