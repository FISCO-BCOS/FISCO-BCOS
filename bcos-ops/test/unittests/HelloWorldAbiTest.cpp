/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/OpsError.h>
#include <bcos-ops/tx/Abi.h>
#include <bcos-ops/tx/HelloWorld.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(HelloWorldAbiTest)

BOOST_AUTO_TEST_CASE(keccakSelectors)
{
    Abi abi(false);
    auto set = toHex(abi.encodeMethodBySignature("set(string)", R"(["x"])"));
    BOOST_CHECK_EQUAL(set.substr(0, 8), "4ed3885e");
    auto get = toHex(abi.encodeMethod(std::string(helloworld::c_abi), "get", "[]"));
    BOOST_CHECK_EQUAL(get, "6d4ce63c");
    auto setViaAbi = toHex(abi.encodeMethod(std::string(helloworld::c_abi), "set", R"(["x"])"));
    BOOST_CHECK_EQUAL(setViaAbi, set);
}

BOOST_AUTO_TEST_CASE(smSelectors)
{
    Abi abi(true);
    auto set = toHex(abi.encodeMethodBySignature("set(string)", R"(["x"])"));
    BOOST_CHECK_EQUAL(set.substr(0, 8), "3590b49f");
    auto get = toHex(abi.encodeMethod(std::string(helloworld::c_abi), "get", "[]"));
    BOOST_CHECK_EQUAL(get, "299f7f9d");
}

BOOST_AUTO_TEST_CASE(decodeStringOutput)
{
    Abi abi(false);
    // abi.encode("Hello, FISCO BCOS"): offset, length 17, data
    std::string hex =
        "0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000011"
        "48656c6c6f2c20464953434f2042434f53000000000000000000000000000000";
    auto decoded = abi.decodeOutput(std::string(helloworld::c_abi), "get", fromHex(hex));
    BOOST_CHECK(decoded.find("Hello, FISCO BCOS") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(argsToJsonArrayKeepsTypes)
{
    BOOST_CHECK_EQUAL(argsToJsonArray({"x", "12", "true", "[1,2]"}), R"(["x",12,true,[1,2]])");
    BOOST_CHECK_EQUAL(argsToJsonArray({}), "[]");
    BOOST_CHECK_EQUAL(argsToJsonArray({"0x1234"}), R"(["0x1234"])");
}

// "010" is not JSON, so it would travel as text that the codec's u256(str) reads as octal 8:
// refused, a quoted "010" is the way to pass it as text. Past 64 bits a number stays digits
// (jsoncpp would make it a double the codec rejects)
BOOST_AUTO_TEST_CASE(argsToJsonArrayRefusesZeroPaddedIntegersAndKeepsBigOnesAsDigits)
{
    BOOST_CHECK_THROW(argsToJsonArray({"010"}), OpsError);
    BOOST_CHECK_THROW(argsToJsonArray({"x", "-07"}), OpsError);
    BOOST_CHECK_EQUAL(argsToJsonArray({R"("010")"}), R"(["010"])");
    BOOST_CHECK_EQUAL(argsToJsonArray({"0", "-0", "10"}), R"([0,0,10])");
    BOOST_CHECK_EQUAL(
        argsToJsonArray({"100000000000000000000000"}), R"(["100000000000000000000000"])");
    BOOST_CHECK_EQUAL(
        argsToJsonArray({"-100000000000000000000000"}), R"(["-100000000000000000000000"])");
    BOOST_CHECK_EQUAL(argsToJsonArray({"1.5"}), R"([1.5])");  // a real number is left to the codec
}

BOOST_AUTO_TEST_CASE(binariesAreHex)
{
    BOOST_CHECK(helloworld::c_binKeccak.size() % 2 == 0);
    BOOST_CHECK(helloworld::c_binSm.size() % 2 == 0);
    BOOST_CHECK(helloworld::c_binKeccak.find("4ed3885e") != std::string_view::npos);
    BOOST_CHECK(helloworld::c_binSm.find("3590b49f") != std::string_view::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
