/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <stdexcept>

using namespace bcos;
using namespace bcos::codec::abi;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ContractABICodecOutTest, TestPromptFixture)

// abiOutByFuncSelector decoding the "address" branch: encode an address, decode
// it back by type name, and confirm the round-trip.
BOOST_AUTO_TEST_CASE(abiOutAddressRoundtrip)
{
    auto hashImpl = std::make_shared<Keccak256>();
    ContractABICodec ct(*hashImpl);
    Address addr("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");
    auto in = ct.abiIn("", addr);

    std::vector<std::string> out;
    std::vector<std::string> types{"address"};
    auto ok = ct.abiOutByFuncSelector(bytesConstRef(&in), types, out);

    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 1U);
    BOOST_CHECK_EQUAL(out[0], addr.hex());
}

// A type outside {int,uint,address,string} hits the default branch and returns
// false without decoding anything.
BOOST_AUTO_TEST_CASE(abiOutUnsupportedTypeReturnsFalse)
{
    auto hashImpl = std::make_shared<Keccak256>();
    ContractABICodec ct(*hashImpl);
    bcos::bytes buffer(32, 0);  // content irrelevant; rejected before any read

    std::vector<std::string> out;
    std::vector<std::string> types{"bool"};
    auto ok = ct.abiOutByFuncSelector(bytesConstRef(&buffer), types, out);

    BOOST_CHECK(!ok);
    BOOST_CHECK(out.empty());
}

// A "string" whose length-prefix offset points past the buffer must be rejected
// by validOffset with std::length_error rather than reading out of bounds.
BOOST_AUTO_TEST_CASE(abiOutStringOffsetOutOfRangeThrows)
{
    auto hashImpl = std::make_shared<Keccak256>();
    ContractABICodec ct(*hashImpl);

    // 32-byte head encoding the string's data offset as 0x1000 (4096), far past
    // the 32-byte buffer. deserialize(str, 4096) -> validOffset(4127) -> throw.
    bcos::bytes buffer(32, 0);
    buffer[30] = 0x10;  // big-endian 0x1000

    std::vector<std::string> out;
    std::vector<std::string> types{"string"};
    BOOST_CHECK_THROW(
        ct.abiOutByFuncSelector(bytesConstRef(&buffer), types, out), std::length_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
