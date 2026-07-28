/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include <bcos-framework/protocol/ProtocolInfo.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>

using namespace bcostars;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ErrorConverterTest)

BOOST_AUTO_TEST_CASE(toTarsErrorFromBcosError)
{
    auto e = bcos::Error::buildError("ctx", 42, "boom");
    auto tars = toTarsError(e);
    BOOST_CHECK_EQUAL(tars.errorCode, 42);
    BOOST_CHECK_EQUAL(tars.errorMessage, "boom");
}

BOOST_AUTO_TEST_CASE(toTarsErrorFromPtrPopulatesFields)
{
    auto ptr = std::make_shared<bcos::Error>(bcos::Error::buildError("ctx", 7, "ptr-boom"));
    auto tars = toTarsError(ptr);
    BOOST_CHECK_EQUAL(tars.errorCode, 7);
    BOOST_CHECK_EQUAL(tars.errorMessage, "ptr-boom");
}

BOOST_AUTO_TEST_CASE(toTarsErrorFromNullPtrYieldsZeroCode)
{
    bcos::Error::Ptr nil;
    auto tars = toTarsError(nil);
    BOOST_CHECK_EQUAL(tars.errorCode, 0);
    BOOST_CHECK(tars.errorMessage.empty());
}

BOOST_AUTO_TEST_CASE(toBcosErrorFromZeroIsNull)
{
    bcostars::Error tars;
    tars.errorCode = 0;
    tars.errorMessage = "ignored";
    BOOST_CHECK(toBcosError(tars) == nullptr);
}

BOOST_AUTO_TEST_CASE(toBcosErrorFromNonZeroWrapsCodeAndMessage)
{
    bcostars::Error tars;
    tars.errorCode = 13;
    tars.errorMessage = "unlucky";
    auto bcos = toBcosError(tars);
    BOOST_REQUIRE(bcos);
    BOOST_CHECK_EQUAL(bcos->errorCode(), 13);
    BOOST_CHECK_EQUAL(bcos->errorMessage(), "unlucky");
}

BOOST_AUTO_TEST_CASE(toBcosErrorFromZeroIntIsNull)
{
    BOOST_CHECK(toBcosError(tars::Int32{0}) == nullptr);
}

BOOST_AUTO_TEST_CASE(toBcosErrorFromNonZeroIntWraps)
{
    auto err = toBcosError(tars::Int32{99});
    BOOST_REQUIRE(err);
    BOOST_CHECK_EQUAL(err->errorCode(), 99);
    BOOST_CHECK_EQUAL(err->errorMessage(), "TARS error!");
}

BOOST_AUTO_TEST_CASE(toUniqueBcosErrorZeroIsNull)
{
    bcostars::Error tars;
    tars.errorCode = 0;
    BOOST_CHECK(toUniqueBcosError(tars) == nullptr);
    BOOST_CHECK(toUniqueBcosError(tars::Int32{0}) == nullptr);
}

BOOST_AUTO_TEST_CASE(toUniqueBcosErrorNonZeroWraps)
{
    bcostars::Error tars;
    tars.errorCode = 21;
    tars.errorMessage = "uniq";
    auto uniq = toUniqueBcosError(tars);
    BOOST_REQUIRE(uniq);
    BOOST_CHECK_EQUAL(uniq->errorCode(), 21);
    BOOST_CHECK_EQUAL(uniq->errorMessage(), "uniq");

    auto uniq2 = toUniqueBcosError(tars::Int32{55});
    BOOST_REQUIRE(uniq2);
    BOOST_CHECK_EQUAL(uniq2->errorCode(), 55);
    BOOST_CHECK_EQUAL(uniq2->errorMessage(), "TARS error!");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ProtocolInfoCodecTest)

BOOST_AUTO_TEST_CASE(roundTripPreservesAllFields)
{
    auto in = std::make_shared<bcos::protocol::ProtocolInfo>();
    in->setProtocolModuleID(bcos::protocol::ProtocolModuleID::GatewayService);
    in->setMinVersion(3);
    in->setMaxVersion(7);

    bcostars::protocol::ProtocolInfoCodecImpl codec;
    bcos::bytes encoded;
    codec.encode(in, encoded);
    BOOST_REQUIRE(!encoded.empty());

    auto out = codec.decode(bcos::ref(encoded));
    BOOST_REQUIRE(out);
    BOOST_CHECK_EQUAL(static_cast<int>(out->protocolModuleID()),
        static_cast<int>(bcos::protocol::ProtocolModuleID::GatewayService));
    BOOST_CHECK_EQUAL(out->minVersion(), 3);
    BOOST_CHECK_EQUAL(out->maxVersion(), 7);
}

BOOST_AUTO_TEST_CASE(roundTripWithZeroVersionRange)
{
    auto in = std::make_shared<bcos::protocol::ProtocolInfo>();
    in->setProtocolModuleID(bcos::protocol::ProtocolModuleID::NodeService);
    in->setMinVersion(0);
    in->setMaxVersion(0);

    bcostars::protocol::ProtocolInfoCodecImpl codec;
    bcos::bytes encoded;
    codec.encode(in, encoded);
    auto out = codec.decode(bcos::ref(encoded));
    BOOST_REQUIRE(out);
    BOOST_CHECK_EQUAL(out->minVersion(), 0);
    BOOST_CHECK_EQUAL(out->maxVersion(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
