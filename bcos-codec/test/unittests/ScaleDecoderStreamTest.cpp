/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-codec/scale/ScaleDecoderStream.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::codec::scale;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ScaleDecoderStreamTest, TestPromptFixture)

// A SCALE compact integer whose flag byte announces a multi-byte encoding but
// whose stream is truncated must raise ScaleDecodeException, not read past end.
BOOST_AUTO_TEST_CASE(compactIntegerTruncatedThrows)
{
    // 0x02: low two bits 0b10 -> 4-byte encoding, needs 3 more bytes; none follow.
    {
        bcos::bytes data{0x02};
        ScaleDecoderStream sd(data);
        CompactInteger v;
        BOOST_CHECK_THROW(sd >> v, ScaleDecodeException);
    }
    // 0x03: low two bits 0b11 -> big-integer encoding of (0>>2)+4 = 4 bytes; none follow.
    {
        bcos::bytes data{0x03};
        ScaleDecoderStream sd(data);
        CompactInteger v;
        BOOST_CHECK_THROW(sd >> v, ScaleDecodeException);
    }
}

// span() exposes the backing buffer and currentIndex() advances as bytes are
// consumed.
BOOST_AUTO_TEST_CASE(spanAndCurrentIndexAccessors)
{
    bcos::bytes data{0x0a, 0x0b, 0x0c};
    ScaleDecoderStream sd(data);

    BOOST_CHECK_EQUAL(sd.span().size(), data.size());
    BOOST_CHECK_EQUAL(sd.currentIndex(), 0U);

    auto first = sd.nextByte();
    BOOST_CHECK_EQUAL(first, 0x0a);
    BOOST_CHECK_EQUAL(sd.currentIndex(), 1U);

    auto second = sd.nextByte();
    BOOST_CHECK_EQUAL(second, 0x0b);
    BOOST_CHECK_EQUAL(sd.currentIndex(), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
