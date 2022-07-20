/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#if 0
#include "bcos-executor/src/precompiled/extension/RingSigPrecompiled.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
namespace bcos::test
{
struct RingSigPrecompiledFixture
{
    RingSigPrecompiledFixture()
    {
        m_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        m_ringSigPrecompiled = std::make_shared<RingSigPrecompiled>(m_hashImpl);
        m_blockContext = std::make_shared<BlockContext>(
            nullptr, m_hashImpl, 0, h256(), utcTime(), 0, FiscoBcosScheduleV4, false, false);
        std::shared_ptr<wasm::GasInjector> gasInjector = nullptr;
        m_executive = std::make_shared<TransactionExecutive>(
            std::weak_ptr<BlockContext>(m_blockContext), "", 100, 0, gasInjector);
    }

    ~RingSigPrecompiledFixture() {}

    bcos::crypto::Hash::Ptr m_hashImpl;
    BlockContext::Ptr m_blockContext;
    TransactionExecutive::Ptr m_executive;
    RingSigPrecompiled::Ptr m_ringSigPrecompiled;
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

    RingSigPrecompiledFixture fixture;
    auto hashImpl = fixture.m_hashImpl;

    bcos::codec::abi::ContractABICodec abi(hashImpl);
    bytes in = abi.abiIn("ringSigVerify(string,string,string)", signature, message1, paramInfo);


    auto ringSigPrecompiled = fixture.m_ringSigPrecompiled;
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    auto execResult = ringSigPrecompiled->call(fixture.m_executive, parameters);
    bytes out = execResult->execResult();

    bool result;
    int retCode;
    abi.abiOut(bytesConstRef(&out), retCode, result);
    BOOST_TEST(true == result);
    BOOST_TEST(0 == retCode);

    // false
    std::string message2 = "ringSigVerify";
    in = abi.abiIn("ringSigVerify(string,string,string)", signature, message2, paramInfo);
    parameters->m_input = bytesConstRef(in.data(), in.size());
    execResult = ringSigPrecompiled->call(fixture.m_executive, parameters);
    out = execResult->execResult();

    abi.abiOut(bytesConstRef(&out), retCode, result);
    BOOST_TEST(false == result);
    BOOST_TEST(retCode == VERIFY_RING_SIG_FAILED);
}

BOOST_AUTO_TEST_CASE(ErrorFunc)
{
    RingSigPrecompiledFixture fixture;
    auto hashImpl = fixture.m_hashImpl;
    auto executive = fixture.m_executive;
    auto ringSigPrecompiled = fixture.m_ringSigPrecompiled;

    bcos::codec::abi::ContractABICodec abi(hashImpl);
    bytes in = abi.abiIn("ringSigVerify(string)", std::string("2AE3FFE2"));
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    auto execResult = ringSigPrecompiled->call(executive, parameters);
    auto out = execResult->execResult();
    bool result;
    int retCode;
    abi.abiOut(bytesConstRef(&out), retCode, result);
    BOOST_TEST(false == result);
    BOOST_TEST(retCode == CODE_UNKNOW_FUNCTION_CALL);
}

BOOST_AUTO_TEST_CASE(InvalidInputs)
{
    RingSigPrecompiledFixture fixture;
    auto hashImpl = fixture.m_hashImpl;
    auto executive = fixture.m_executive;
    auto ringSigPrecompiled = fixture.m_ringSigPrecompiled;

    // situation1
    bcos::codec::abi::ContractABICodec abi(hashImpl);
    bytes in = abi.abiIn("ringSigVerify(string,string,string)", std::string("2AE3FFE2"),
        std::string("2AE3FFE2"), std::string("2AE3FFE2"));
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());

    auto execResult = ringSigPrecompiled->call(executive, parameters);
    bytes out = execResult->execResult();
    int retCode;
    bool result;
    abi.abiOut(bytesConstRef(&out), retCode, result);
    BOOST_TEST(retCode == VERIFY_RING_SIG_FAILED);
    BOOST_TEST(result == false);


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
    parameters->m_input = bytesConstRef(in.data(), in.size());

    execResult = ringSigPrecompiled->call(executive, parameters);
    out = execResult->execResult();

    abi.abiOut(bytesConstRef(&out), retCode, result);
    BOOST_TEST(retCode == VERIFY_RING_SIG_FAILED);
    BOOST_TEST(result == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

#endif