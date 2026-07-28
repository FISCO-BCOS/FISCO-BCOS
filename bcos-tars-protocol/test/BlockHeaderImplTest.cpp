/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include <boost/test/unit_test.hpp>
#include <utility>

using namespace bcostars::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(TarsBlockHeaderImplTest)

// The bcostars::BlockHeader& constructor wraps an external tars struct by
// reference, so reads/writes go through to it; inner()/setInner() expose it.
BOOST_AUTO_TEST_CASE(referenceConstructorInnerAndSetInner)
{
    bcostars::BlockHeader tars;  // must outlive the wrapper below
    tars.data.version = 3;
    BlockHeaderImpl header(tars);
    BOOST_CHECK_EQUAL(header.version(), 3U);

    // mutable inner() writes back to the referenced struct
    header.inner().data.version = 5;
    BOOST_CHECK_EQUAL(header.version(), 5U);
    BOOST_CHECK_EQUAL(std::as_const(header).inner().data.version, 5);
    // const inner() must return a reference to the same external struct, not a
    // copy — a copy would still satisfy the value check above. Compare addresses.
    BOOST_CHECK(&std::as_const(header).inner() == &tars);

    // setInner replaces the referenced struct's contents
    bcostars::BlockHeader other;
    other.data.version = 7;
    header.setInner(other);
    BOOST_CHECK_EQUAL(header.version(), 7U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
