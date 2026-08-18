/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-codec/abi/ContractABIType.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::codec::abi;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ContractABITypeParseTest, TestPromptFixture)

// reset() maps each elementary type name to a valid enum; "address" and the
// valid() query were previously unexercised.
BOOST_AUTO_TEST_CASE(resetElementaryTypesAndValid)
{
    ABIInType t;

    BOOST_CHECK(t.reset("address"));
    BOOST_CHECK(t.valid());
    BOOST_CHECK_EQUAL(t.getEleType(), "address");

    BOOST_CHECK(t.reset("bool"));
    BOOST_CHECK(t.valid());

    BOOST_CHECK(t.reset("bytes"));
    BOOST_CHECK(t.valid());

    // An unrecognised elementary type leaves aet == INVALID: reset returns false
    // and valid() reports false.
    BOOST_CHECK(!t.reset("notatype"));
    BOOST_CHECK(!t.valid());
}

// The array-suffix parser rejects malformed extents: an unterminated bracket
// and a non-numeric extent both fail.
BOOST_AUTO_TEST_CASE(resetRejectsMalformedArrayTypes)
{
    ABIInType t;

    // '[' with no matching ']': the scan finds npos for the right bracket.
    BOOST_CHECK(!t.reset("int["));

    // Non-digit inside the brackets fails the all-of-digits check.
    BOOST_CHECK(!t.reset("int[a]"));

    // A well-formed fixed and dynamic array still parse.
    BOOST_CHECK(t.reset("int[2]"));
    BOOST_CHECK(t.reset("int[]"));
}

// removeExtent pops one array dimension; on a scalar (rank 0) it returns false.
BOOST_AUTO_TEST_CASE(removeExtentOnScalarReturnsFalse)
{
    ABIInType t;
    BOOST_CHECK(t.reset("uint256"));
    BOOST_CHECK_EQUAL(t.rank(), 0U);
    BOOST_CHECK(!t.removeExtent());  // nothing to remove

    BOOST_CHECK(t.reset("uint256[3][4]"));
    BOOST_CHECK_EQUAL(t.rank(), 2U);
    BOOST_CHECK(t.removeExtent());
    BOOST_CHECK(t.removeExtent());
    BOOST_CHECK(!t.removeExtent());  // now empty again
}

// dynamic() is true for string/bytes and for any array with an empty extent.
BOOST_AUTO_TEST_CASE(dynamicClassification)
{
    ABIInType t;
    BOOST_CHECK(t.reset("string"));
    BOOST_CHECK(t.dynamic());

    BOOST_CHECK(t.reset("uint256[]"));
    BOOST_CHECK(t.dynamic());

    BOOST_CHECK(t.reset("uint256[2]"));
    BOOST_CHECK(!t.dynamic());
}

// ABIFunc::parser rejects a signature lacking a valid "(...)" clause.
BOOST_AUTO_TEST_CASE(parserRejectsMalformedSignature)
{
    ABIFunc f;
    BOOST_CHECK(!f.parser("noparens"));  // no '('
    BOOST_CHECK(!f.parser("bad)("));     // ')' before '('

    // A well-formed signature still parses.
    BOOST_CHECK(f.parser("transfer(address,uint256)"));
    BOOST_CHECK_EQUAL(f.getFuncName(), "transfer");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
