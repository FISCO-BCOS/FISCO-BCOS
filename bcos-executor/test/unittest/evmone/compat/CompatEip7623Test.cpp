/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-7*: EIP-7623 calldata floor formula.
 *  @file CompatEip7623Test.cpp
 */

#include "Common.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatEip7623)

BOOST_AUTO_TEST_CASE(FC_7_calldata_floor_formula)
{
    using namespace bcos::executor;

    bytes empty;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(empty)), 0);
    bytes zeros(100, 0x00);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(zeros)), 1000);
    bytes nonzero(100, 0xff);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(nonzero)), 4000);

    bytes mixed(100);
    for (int i = 0; i < 50; ++i)
        mixed[i] = 0x00;
    for (int i = 50; i < 100; ++i)
        mixed[i] = 0x42;
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), 2500);

    bytes oneNonZero{0x42};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneNonZero)), 40);
    bytes oneZero{0x00};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneZero)), 10);
    bytes fourZeros(4, 0x00);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(fourZeros)), 40);
    bytes oneOne{0x42, 0x00};
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(oneOne)), 50);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEip7623
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
