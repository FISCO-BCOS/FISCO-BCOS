/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;

namespace bcos::test
{
// The Liquid (WASM) ABI codec is partly TODO (uint/int/bytes/string ser/deser are
// no-ops), so most checks are NO_THROW / CHECK_THROW rather than round-trip. bool
// and bytesN are fully implemented and round-trip. Address and fixed/ufixed throw.
BOOST_AUTO_TEST_SUITE(ContractABITypeCodecLiquidTest)

BOOST_AUTO_TEST_CASE(liquidBaseTypeSerialize)
{
    ContractABITypeCodecLiquidImpl codec;
    bcos::bytes buffer;

    BOOST_CHECK_NO_THROW(codec.serialize(u256(5), 256, buffer));   // no-op (TODO)
    BOOST_CHECK_NO_THROW(codec.serialize(s256(-3), 256, buffer));  // no-op (TODO)

    codec.serialize(true, buffer);  // pushes 1
    BOOST_CHECK_EQUAL(buffer.back(), 1);
    codec.serialize(false, buffer);  // pushes 0
    BOOST_CHECK_EQUAL(buffer.back(), 0);

    bcos::bytes fixed{1, 2, 3};
    BOOST_CHECK_NO_THROW(codec.serialize(fixed, 0, buffer));  // bytesN
    bcos::bytes tooBig(33, 0);
    BOOST_CHECK_THROW(codec.serialize(tooBig, 0, buffer), std::runtime_error);  // > 32

    BOOST_CHECK_NO_THROW(codec.serialize(bcos::bytes{4, 5}, buffer));  // bytes
    BOOST_CHECK_NO_THROW(codec.serialize(std::string("hi"), buffer));  // string

    // address is unsupported in the liquid abi
    BOOST_CHECK_THROW(
        codec.serialize(Address("0xbe5422d15f39373eb0a97ff8c10fbd0e40e29338"), buffer),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(liquidBaseTypeDeserialize)
{
    ContractABITypeCodecLiquidImpl codec;
    bcos::bytes buffer{1, 2, 3};

    bool b = false;
    codec.deserialize(b, buffer, 0);  // implemented
    BOOST_CHECK(b);

    u256 u = 0;
    BOOST_CHECK_NO_THROW(codec.deserialize(u, buffer, 0));  // no-op
    s256 s = 0;
    BOOST_CHECK_NO_THROW(codec.deserialize(s, buffer, 0));  // no-op

    bcos::bytes fixedOut;
    codec.deserialize(fixedOut, buffer, 0, 3);  // bytesN
    BOOST_CHECK_EQUAL(fixedOut.size(), 3U);

    bcos::bytes bytesOut;
    BOOST_CHECK_NO_THROW(codec.deserialize(bytesOut, buffer, 0));  // no-op
    std::string strOut;
    BOOST_CHECK_NO_THROW(codec.deserialize(strOut, buffer, 0));  // no-op

    Address addr;
    BOOST_CHECK_THROW(codec.deserialize(addr, buffer, 0), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(liquidAbstractTypeSerialize)
{
    ContractABITypeCodecLiquidImpl codec;
    bcos::bytes buffer;

    // a struct exercising the BOOL/UINT/INT/STRING/FIXED_BYTES/DYNAMIC_BYTES/
    // DYNAMIC_LIST/FIXED_LIST/STRUCT dispatch branches
    auto root = Struct::newValue();
    root->addMember(Boolean::newValue(true));
    root->addMember(Uint::newValue(u256(7)));
    root->addMember(Int::newValue(s256(-2)));
    root->addMember(String::newValue("liquid"));
    root->addMember(bcos::cppsdk::abi::FixedBytes::newValue(3, bcos::bytes{1, 2, 3}));
    root->addMember(DynamicBytes::newValue(bcos::bytes{9, 8}));

    auto dynList = DynamicList::newValue();
    dynList->addMember(Uint::newValue(u256(1)));
    dynList->addMember(Uint::newValue(u256(2)));
    root->addMember(std::move(dynList));

    auto fixedList = FixedList::newValue(2);
    fixedList->addMember(Uint::newValue(u256(3)));
    fixedList->addMember(Uint::newValue(u256(4)));
    root->addMember(std::move(fixedList));

    // the ADDRESS abstract branch serializes the address as a string (does not
    // throw, unlike the raw Address overload)
    root->addMember(Addr::newValue("0xbe5422d15f39373eb0a97ff8c10fbd0e40e29338"));

    BOOST_CHECK_NO_THROW(codec.serialize(*root, buffer));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
