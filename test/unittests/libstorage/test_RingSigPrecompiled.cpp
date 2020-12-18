/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file test_RingSigPrecompiled.cpp
 *  @author shawnhe
 *  @date 20190827
 */

#include "Common.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libprecompiled/extension/RingSigPrecompiled.h>
#include <libprotocol/ABI.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::precompiled;

namespace test_RingSigPrecompiled
{
struct RingSigPrecompiledFixture
{
    RingSigPrecompiledFixture()
    {
        context = std::make_shared<ExecutiveContext>();
        ringSigPrecompiled = std::make_shared<RingSigPrecompiled>();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        ringSigPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~RingSigPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    RingSigPrecompiled::Ptr ringSigPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(test_RingSigPrecompiled, RingSigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(TestRingSigVerify)
{
    // true
    std::string signature =
        "eyJDIjogIjU4MDk3MjI0NzExMDQwOTI0NzQzMDI1OTQ4MTM3MTUzNzA0MzM5MjIwODE2NTY5MjYzNDM1NTEyOTUwNj"
        "Y5NTIxMDA0MTAxNTQ2MzIzLiIsIlkiOiAiMTcxOTc4NjM0MzM1MzM5NjU4OTUxNzI5Mzg0MDM3ODU5MDk3NTc5MDA4"
        "NzU3MTAxMjI3MTY3NzE2MDEzMzM3MjEzNzcwMjUxMzA2MTY2NjE5NDEzMzEyMzMyMDIxMDA4ODI5NjQ2MjA5OTY1Nj"
        "gwNzU1NzgxNjgyNjc2OTg1NTA3MDYyNDM0NTAzNjM3OTIzMDQzNTMxNzk2MzExMjIxOTY5NDU4NTM1NTY0OTg4MDI4"
        "MTYxMDAzNjc5NzI2NTQ1Mjg3ODY1NzE3MjEyNTQyOTU4MDc0NjIxMDE5MTc1OTI3NzcxNjc0NDA5ODA1MTc2NTcyMT"
        "gzOTQ4MzM2NjMyNjA1NDI2ODEzMzExMTc4MDIzNjY0MTg1MzkxMTgyMTg2NjI1ODYxODYyNjkxNzk2OTA2MTY1LiIs"
        "Im51bSI6ICIyIiwicGswIjogIjYxNjQ5MTE2MzI3Mjk5MjUyMTMyNDc4NjY2Njc1MDE1MzY0NDE1OTM2NzE0ODk1Nj"
        "czMjE1MzU2NTA5MTg1MjY5OTcxODc1NTMyOTMzNjg3MTk4ODIxNjcxMTk1MDExOTMzMTg3ODQ2MTA3ODE4MjQ5Mzc3"
        "NDAwMTc3MzIwNjQzMDYwMjAwMzUwMjM5MDM4NDA4MjczNTA1NTMyMzE0NTc3NjkyNjczNzQyNjM2NDI3MTc2MjYyMz"
        "E1OTg4NDEyMDkwNjMyNDI5ODE0MDgxNjk4MTAwODcyMTI1NjQ5MjY5OTU1NDQ0NTYyMjM3NDY1NzI0NjAxNzU3MTQ2"
        "ODA1MTg4MDk5MTUxNTQwMTE1NDQ4MjM2ODcyMTQ5MTk1MjQwNzUzNzU5MzI5NDA1NDAxMDIwNjg4LiIsInBrMSI6IC"
        "IxMzk2OTMyMzA0NjMyOTMyMDE2MTYyNzk5ODc0ODMwNzExMzU5OTY5NzYxNTUyNDUwOTk3MTEwMDM0NDEwMzE1NTY2"
        "MDE2NTA2MDQwOTU0NjA4OTAxMTE1OTc2NDUyMDI2MDMyOTcxNTQzNDUyNzgyMjUzNDc3NjU4NzQ4NjI2MjUzMjY0MD"
        "I0MzM4OTE1MjM4NTg0Njc5NDI1Mjk2NTUxOTgyOTY1OTY3NjYyNzUxMjA2NDQ0NzU1ODg4NjU3MzA4MjgzNjA1MzE4"
        "MjU1NzY2NTIxMTE3MTc5NTczNjU0Nzc4Njc3NDY1OTAxMTA5NTg0Mzg2ODgzNzA0NjU4OTQ5MzM0Nzg3MTE0NjAwNz"
        "UyMTg0NTk0NDgzNjk1NTk1MDYzMDY4NzQ4ODk4OTU1MTAxOTkyNDYuIiwiczAiOiAiMjAxMDcyNTE3OTI3MzQwMDE1"
        "NzY2NzM0NTE3NzQ1MjgzMjc4NDAyODM1MjQ0OTAzNzAzNDA0MTkwODE4NDI2NDUwMDkzODQ2Nzc0MDQ5NTMwMjY1ND"
        "M0MDMxNjY5MzA5NDc5MzU4MzIxNDE0OTAzNzEyNzk0MTI5NzQ1MTYxNDA3Mjc0OTI5NTYyNjk0ODk3MDYwNTAyNjcx"
        "Njk0NDIyNzY4OTkyMjI2MTAwMDg1NDcyNTg0ODE3Nzg2ODIzODk4Mzc2MjIxMzA5MjU4NzcxODUwODUzMjU2Nzc2Nj"
        "U2MzI4Nzg0NzE3MTUxNzg0NDI4NTQ1Njg5MDA4MzIwMzA1ODYzMTAyMTQyNDY1MjkzODgwNTczNjAxNDYwMDM1MzIw"
        "MDgzMzIyMTcyNDUyMzEwNDIxOTcuIiwiczEiOiAiNjkyNTY3NzY0NTc4MjY4Mjk0MTYwMTEzMjY5MjM1NTQwMzM5ND"
        "ExMjgzMjY0MjM4NDIwMTc4Njk3ODM4OTE1MDg4NTcyNzQ1MjAxMjA5NTcyNDQzNDIyNDY0MzA4MTA2MTUwMTg4MTk5"
        "MjM4MjE1MDg5MzgxODY1NDYzNTUyNDkxOTY0OTg2MzA0NDU4MTQ4ODMzOTEzNzk3MDc0OTQyNTMxMjEzNDU3MDE0Mj"
        "YzNDc4ODY1Mjc3MzMwNjYwMjM5OTE2NzQxMDY5MzE0ODg3MzA4NjUxODkyMjA5MTY2OTkyNDk0NjY2OTc0NDI1OTAz"
        "ODc5NDgzMTA0NzUyNjQ3MTg0NDYzODQyNzUzMDUwNDAyODg2NjMyNzIwNzE4MDEwMTkxNzU3ODI0MTI0ODI3MTM4Nj"
        "UuIn0=";
    std::string message1 = "test ringSigVerify";
    std::string paramInfo =
        "eyJnIjogIjMuIiwicCI6ICIxNzc4NzYxMzUxNzk5MTA2NjkyODQwMjg1MDQ1NDQ5NzMxNDM2MTI0NDI1NTE0MTk4Nj"
        "U0ODQ4MzA5MzE2NjYxOTQzMjkwNDg5ODIyMzgzNTgxMzQzMDk3NTc3MTQyMjU4NDg2NzI3NTkyNzU2NzQxMzc4MjI0"
        "NjQ0Nzk5NjEwNjAxMDY3MzQ4NzgwNjA0MjQ5NTMyOTQwODQxNTEyOTE3Nzg0NTQxODIwOTI4OTk2NjE0MDIwNzgxNj"
        "kzODU4NTcwMjM2NzYwMTI3OTc5ODQxNTU3MzAxMzM0NzQ4MTY1OTg4NDExMDAyMzczNjMxNjg2NjY3MDUzMjk5Mjk3"
        "NjA4MjU2OTI3Mzk2NzQ1MTkzMzU2Nzg3NzA5NjQ1MzQ0MzA5MDUxNjkyNzk0MTIxOTA2MDY4MTkuIiwicSI6ICI4OD"
        "kzODA2NzU4OTk1NTMzNDY0MjAxNDI1MjI3MjQ4NjU3MTgwNjIyMTI3NTcwOTkzMjc0MjQxNTQ2NTgzMzA5NzE2NDUy"
        "NDQ5MTExOTE3OTA2NzE1NDg3ODg1NzExMjkyNDMzNjM3OTYzNzgzNzA2ODkxMTIzMjIzOTk4MDUzMDA1MzM2NzQzOT"
        "AzMDIxMjQ3NjY0NzA0MjA3NTY0NTg4OTIyNzA5MTA0NjQ0OTgzMDcwMTAzOTA4NDY5MjkyODUxMTgzODAwNjM5ODk5"
        "MjA3Nzg2NTA2NjczNzQwODI5OTQyMDU1MDExODY4MTU4NDMzMzM1MjY2NDk2NDg4MDQxMjg0NjM2OTgzNzI1OTY2Nz"
        "gzOTM4NTQ4MjI2NzIxNTQ1MjU4NDYzOTcwNjA5NTMwMzQwOS4ifQ==";
    bcos::protocol::ContractABI abi;
    bytes in = abi.abiIn("ringSigVerify(string,string,string)", signature, message1, paramInfo);
    auto callResult = ringSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    bool real_result1 = true;
    bool result1;
    abi.abiOut(bytesConstRef(&out), result1);
    BOOST_TEST(real_result1 == result1);

    // false
    std::string message2 = "ringSigVerify";
    in = abi.abiIn("ringSigVerify(string,string,string)", signature, message2, paramInfo);
    callResult = ringSigPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    bool real_result2 = false;
    bool result2;
    abi.abiOut(bytesConstRef(&out), result2);
    BOOST_TEST(real_result2 == result2);
}

BOOST_AUTO_TEST_CASE(ErrorFunc)
{
    bcos::protocol::ContractABI abi;
    bytes in = abi.abiIn("ringSigVerify(string)", std::string("2AE3FFE2"));
    auto callResult = ringSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == CODE_UNKNOW_FUNCTION_CALL);
}

BOOST_AUTO_TEST_CASE(InvalidInputs)
{
    // situation1
    bcos::protocol::ContractABI abi;
    bytes in = abi.abiIn("ringSigVerify(string,string,string)", std::string("2AE3FFE2"),
        std::string("2AE3FFE2"), std::string("2AE3FFE2"));
    auto callResult = ringSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == VERIFY_RING_SIG_FAILED);

    // situation2
    std::string signature2 =
        "eyJDIjogIjU4MDk3MjI0NzExMDQwOTI0NzQzMDI1OTQ4MTM3MTUzNzA0MzM5MjIwODE2NTY5MjYzNDM1NTEyOTUwNj"
        "Y5NTIxMDA0MTAxNTQ2MzIzLiIsIlkiOiAiMTcxOTc4NjM0MzM1MzM5NjU4OTUxNzI5Mzg0MDM3ODU5MDk3NTc5MDA4"
        "NzU3MTAxMjI3MTY3NzE2MDEzMzM3MjEzNzcwMjUxMzA2MTY2NjE5NDEzMzEyMzMyMDIxMDA4ODI5NjQ2MjA5OTY1Nj"
        "gwNzU1NzgxNjgyNjc2OTg1NTA3MDYyNDM0NTAzNjM3OTIzMDQzNTMxNzk2MzExMjIxOTY5NDU4NTM1NTY0OTg4MDI4"
        "MTYxMDAzNjc5NzI2NTQ1Mjg3ODY1NzE3MjEyNTQyOTU4MDc0NjIxMDE5MTc1OTI3NzcxNjc0NDA5ODA1MTc2NTcyMT"
        "gzOTQ4MzM2NjMyNjA1NDI2ODEzMzExMTc4MDIzNjY0MTg1MzkxMTgyMTg2NjI1ODYxODYyNjkxNzk2OTA2MTY1LiIs"
        "Im51bSI6ICIyIiwicGswIjogIjYxNjQ5MTE2MzI3Mjk5MjUyMTMyNDc4NjY2Njc1MDE1MzY0NDE1OTM2NzE0ODk1Nj"
        "czMjE1MzU2NTA5MTg1MjY5OTcxODc1NTMyOTMzNjg3MTk4ODIxNjcxMTk1MDExOTMzMTg3ODQ2MTA3ODE4MjQ5Mzc3"
        "NDAwMTc3MzIwNjQzMDYwMjAwMzUwMjM5MDM4NDA4MjczNTA1NTMyMzE0NTc3NjkyNjczNzQyNjM2NDI3MTc2MjYyMz"
        "E1OTg4NDEyMDkwNjMyNDI5ODE0MDgxNjk4MTAwODcyMTI1NjQ5MjY5OTU1NDQ0NTYyMjM3NDY1NzI0NjAxNzU3MTQ2"
        "ODA1MTg4MDk5MTUxNTQwMTE1NDQ4MjM2ODcyMTQ5MTk1MjQwNzUzNzU5MzI5NDA1NDAxMDIwNjg4LiJ9";
    std::string message2 = "ringSigVerify";
    std::string paramInfo2 =
        "dHlwZSBhIHEgODA0NDY4NDc1Nzk2NTU1ODI1OTc0NDQxNDk5ODkyMzUwNzY3NjQ4NzYxOTQ5MjM1NTQzNjAyNjYzND"
        "EzNjg2NjIzMzA1ODQxODA0NDEyODE4NjA4MTEyNDU3ODkwMDE0MjA1NjYxNDAxOTExNDkxMTg5MTYzMDUxMjI1MjMy"
        "OTY4NzE2Nzk0MTk2Nzg2MDE4NjgyNjY3MDA4MDU5IGggNjAgciAxMzQwNzgwNzkyOTk0MjU5NzA5OTU3NDAyNDk5OD"
        "IwNTg0NjEyNzQ3OTM2NTgyMDU5MjM5MzM3NzcyMzU2MTQ0MzcyMTc2NDAzMDA3MzU0Njk3NjgwMTg3NDI5ODE2Njkw"
        "MzQyNzY5MDAzMTg1ODE4NjQ4NjA1MDg1Mzc1Mzg4MjgxMTk0NjU2OTk0NjQzMzY0NDcxMTExNjgwMSBleHAyIDUxMi"
        "BleHAxIDMyIHNpZ24xIC0xIHNpZ24wIDE=";
    in = abi.abiIn("ringSigVerify(string,string,string)", signature2, message2, paramInfo2);
    callResult = ringSigPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == VERIFY_RING_SIG_FAILED);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_RingSigPrecompiled
