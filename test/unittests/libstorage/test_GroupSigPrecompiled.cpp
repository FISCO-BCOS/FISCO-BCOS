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
/** @file test_GroupSigPrecompiled.cpp
 *  @author shawnhe
 *  @date 20190827
 */

#include "Common.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libethcore/ABI.h>
#include <libprecompiled/extension/GroupSigPrecompiled.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;

namespace test_GroupSigPrecompiled
{
struct GroupSigPrecompiledFixture
{
    GroupSigPrecompiledFixture()
    {
        context = std::make_shared<ExecutiveContext>();
        groupSigPrecompiled = std::make_shared<GroupSigPrecompiled>();

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        groupSigPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~GroupSigPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    GroupSigPrecompiled::Ptr groupSigPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(test_GroupSigPrecompiled, GroupSigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(TestGroupSigVerify)
{
    // true
    std::string signature =
        "eyJUMSI6ICIxY2ZmMmRiMDUzNGQ3OWJmMjFmMmI1ZWIxZTQ3ZjcxMjgzNTRhYmZjNDNiZDQyZTdiNDUwZTUwZTE1ZT"
        "VkOWZmMjNiOWFiNGNiZTQwODhhZGYxZGE3MjFlNTFmMjc5NTFhNjUyYTVmNjQ1MDM2MjBhOThiYjZjYWIxMjg3MWUz"
        "Njg1MzdjMjVmZGUyMWRhMTM3ZjM3Zjg1MGZlMWFlMjcwN2RlODZjNGE0MTliOGY4NzFmOWQ5Yzc0NDdhMGMxNDQ3OG"
        "E0MThlYWQzMWM3NTYzY2ZlMmM3ZjY0ZGVjODJiY2JjOTUyNTZhOGNmMGY2ZTliN2I4N2RiYmFhNWM0OTU4MWU2MiIs"
        "IlQyIjogIjBjZDlhYTAzYjgyYjJkYjcwOGRhYjEzZDcxZDA5YTBmYThkZWRjODA1MGFiMjA0ZGYyNzBmMGU4NTA4YT"
        "hlMTg4NTJhYTVmMTUzNjBkYzdiMThjNWNkNjk4MjcyOGVmYzVjMTU3MjBjZTkwZjMzY2JmN2FlYjM2MzRlNWQ1NmQw"
        "ZjczMzdjZjllYjM5ZWZiZTIzYWZhMjNjNTU0ODc4MGY2ZDUwMzk1NzhiNjE1YjdmOTBmOTdiNWExZThmN2Y3ZjE3Yz"
        "QzNjZiMmRhYWMwODdlZjc4YjY3YmZhOWZjMDBjZGRhMDM0Y2I0OTIzMjEzYmE1NjMyNmUzZWUzOWZhYzM3NWM1Iiwi"
        "VDMiOiAiMGEwYjJhNTc5NjNlMWZmMGNiZjZmZjQyMjQ2MDFmMTY4MzA2ZWI1ZmU1MTY1NDMzMmIxYjU5N2UxYjA1ZG"
        "QxM2NjZjY2MmJkN2VhMWNjZTNjYmQ1MTM1MzBkNzVhY2ZmMWMwY2JhMzg3ODI2YTk1NDk0MDZlY2FmNjM0M2EwYmY2"
        "OTI2MzkyZDUwMGE0NzRhZDQ3NzQwMWNhMjliZmZmODJhYjc5NDJkMjJlNzBkOGJmZTUzMGUxMzQ1MWY4YWRjMThhOT"
        "QwOWI5NDIzZTg5NzgzOGZjNjk3MGQyMTYyYTg3OGU2YjkxYTlhZWM5Nzk3MWNlMmQxOTUxYTJiOTgzNzAwZTAiLCJj"
        "IjogIjdiMmU3ZDlmYjY5ODBiODhjYjMxY2QxZjQ1N2IxZDVkODI5NTYwNjI3MDU1NDZlMDY5NTc4YTA4YzRjNWJmNz"
        "MwMDdiMmU3ZDlmYjY5ODBiODhjYjMxY2QxZjQ1N2IxZDVkODI5NTYwNjI3MDU1NDZlMDY5NTc4YTA4YzRjNWJmIiwi"
        "cmFscGhhIjogIjE1ZmUxZmI1NGRhYTBlMTY0YTc0YmE1YWYxOGYzOTdiYjk4NmViYmEzODg0YzI4MzA1Y2I5MmQ3NT"
        "M4ODdmYWZmMjFhMWNkNTQ3OGQyMTBjMGU5ZmZkNDU5MDlkMjY1M2E4MjY0ZWM3ODllMjY3ZDk2NzA0MjIzN2I0Y2M4"
        "MDBlIiwicmJldGEiOiAiMTE0NDliYzhhYTMxMmIwOTQ2ZmVhNjliOWUxMTljMjBlNDEzYzIwODlmYjVkMmRmZjExND"
        "FkMDllMjE4M2UwMmI2YTUzZjY0ODY3ODY3MWQxMWU2ODdjMTk0ZGJkYThmNzM5OWRhMTAyM2I3NzAyNGYzNjlkMzg2"
        "YTE1MTZhYjkiLCJyZGVsdGExIjogImY3ZGI5ZWM2ODIwNGJjNDUxMjk3ZjY0NWQ1ZTk5ZDk1NDY1ZWQ3ZjUwYTgwND"
        "VmYjcyMDJkN2ZhYTUxMGUyOGFjMDk1NWIwOGZiZTY5ZWU4Yjg3MGY2NjRhNjI3OTMwM2ViZjU3MmM4MzAzOTYwYTkz"
        "MjFlZTY3ZTExN2JlNTVkIiwicmRlbHRhMiI6ICJkMzRhNTQ3MGU2ZWI1ZjdjZmRhNjIzYmVmYjVkNjQyYzFlMDY1Nj"
        "lhOTAzODI5MDc5ODVkZTQ2NGUzYmUxZWQyMDViMzZmZjllNjY2NGZlMjhiMjBjZjdkMTQxYThmMGIyNTg1YTFiNjUx"
        "OGQ3ZmFmODFhZWFlNzAzYmVhMWRiNSIsInJ4IjogIjY2N2NmMDk0NjdmNWY1MTMyZWY1NjUyYmI5ZDRiMDdmN2UxND"
        "VkZDdiMDU2OGY2ZjkyMmM4OGZjOGE3Njg0MTA3ZWRlYjVmYTk3ZTA5OTE5ZTc4ZjM3M2NkMDFiY2ZlYmQwZGNhYjZm"
        "ZjYzMWQ4OGM5OTBiYmQwMGEzNmFiZTgwIn0=";
    std::string message1 = "test groupSigVerify";
    std::string gpkInfo =
        "eyJnMSI6ICIxZmNkZWQ1ZGU0Zjc3ZTQ2MWE4N2Q0ZmY4ZmY5YjI1ZDQ5MzkxYjY2Y2MyNGUxN2E2NjYxODRhZDBhYm"
        "IzZDcwYTc4MDgwNTNjMjAxODhiZjA0NjBiZTA1YjQ0YzEyYmFhMzVhNjUxM2YwM2ZlZTVjNjU3ZmRlYzA1MDBlNGVj"
        "YTY1MTcyYzI2MmNiNzExMGNhZGE5ZDcyZTNmYmFiZTcxYThjOWFjNDZiMTA5MTVjNGIzNGZmZjQyOWVjMGJiNjU0ZD"
        "Y3YzVlNjA2NzI3YjIxM2ZhY2JkMDA1N2ZjYjE3ZWYyM2QwOTNkNGY2ZmUxZjk2NmMzYmFjMjk4NjI4ZTJjZGMwMyIs"
        "ImcyIjogIjI2MDhlZDNlODU3ZmFlZTllZDRiNjVkZjY5MTQzMzdlOTg5N2Q1Mjc1NGJlODg2ZDc4ZDFiMWFiMGQxMT"
        "czZTQyNjBlYWVlOTg5YjY3MjdmZDFhNDdmMGE1Y2M1M2IyNDY3NzBhYTcxOGIyOGI1NmJhODJjZDNhM2M2NzgxMzkw"
        "YWQzMjU2NGVhMzU3MDFmZWViOTExZWQzNzA5YjQ1MmI2N2RiMjhiNmUxZmZmZWZiZTE5Y2M2NzBkOTRkN2NhMDZiMm"
        "RlNmU5YzVmYWI2ZjMxMjE0Njk2NjRlYWZkNWNkZWNlNmZhMGU3ZTE0MjEyMTY3OTE5MDI1N2JjMWE4MDc5M2VjIiwi"
        "aCI6ICIwOGU4M2U0MWM5MDJlNmM5NzQxYTQ3YTg5MzRkOTBjNTgyZDBiZTUyZjBiOTMyZTU0OWJhZDU1MWYzNDdlYz"
        "FmYmFlMjkzOGNjOWNiNGI5NzA1NjgzNjI2YmRiYzNlYjBjMDk0NzYxNmI3NTNiOGJiMDRkZjg1ZmI0NmZiZGE5ZDQx"
        "MmZkMTc5MjU4ZWVkODllMTRjNTczOGU4ZDIwOTAxOGI2NTQ1N2Q0ZmQ4YzRjZTY1NWJmYzY2MGNiOWI5ZWVmOTgwMT"
        "M3MWM0MGY2ZTRlOGYyODUyZWM0MjVlOTlkNDhkNzFmMTA4YWJiMzA0OWE2YjAyODA5NTNmYWNmOWFlNWE2OSIsInBy"
        "X2cxX2cyIjogIjM0MjFhN2FlMmUwNjE0MDQ4NGFmMTgzYzQ1MWZiYzAwYWU4ZDhmYzE0ZDBhOTlmMWM1OTI3MmQwOT"
        "Y2ZDNiYzdjOTVkZjdjZDI3NTI1YmI0ZTcwNGNjYzM2NGNiNTAwMTdhZjUzOWRhNTgyNzFhZjgyODViZGNhYjc4NTg4"
        "NzQ4ODMyYjZhODFlZjEzODEwYWJlMGU0M2ViMmRiN2UyODliODU0MGVjMmEzNmRiZDc3YWQ3OTgyNzc5YTJjZTYyZj"
        "M2YWI4MjczMzcxYmQwODJmZTJjNzNiMjYwNjE1MDlkYmVjZDFmMjI0MjA2M2MyZDk1ZDdiZjZmNDRjMzIzODlhNGM1"
        "IiwicHJfZzFfZzJfaW52IjogIjM0MjFhN2FlMmUwNjE0MDQ4NGFmMTgzYzQ1MWZiYzAwYWU4ZDhmYzE0ZDBhOTlmMW"
        "M1OTI3MmQwOTY2ZDNiYzdjOTVkZjdjZDI3NTI1YmI0ZTcwNGNjYzM2NGNiNTAwMTdhZjUzOWRhNTgyNzFhZjgyODVi"
        "ZGNhYjc4NTg4NzQ4ODMxMDk1N2UxMGVjN2VmNTQxZjFiYzE0ZDI0ODFkNzY0N2FiZjEzZDVjOTI0Mjg4NTI4NjdkOD"
        "g2NWQzMTlkMGM5NTQ3ZDhjYzhlNDJmN2QwMWQzOGM0ZDlmOWVhZjYyNDEzMmUwZGRiZGY5YzNkMjZhMjg0MDkwYmIw"
        "MGRjNzY1Yjc2IiwicHJfaF9nMiI6ICIwZjBjYmJmOTI4ZTI1YTQwODZhMGE3OWMwOTQzOGFiY2Q5NzhiNjkzZmYxNz"
        "djMGNmZTYzZWNjM2IzOGQ1OWE0NDc5ZDhjNjc2NGEwZGMyMzdjZjViMTAxMzg0Y2E4NThjNWIzYjk2ZjdhZmYxMWJi"
        "MDdlZjM3ZTI0YmExZTMzOGNmMmNkMWY5N2I4YWNjOWJjNTgwMGRlMDc4MmRiZTZhMDQ1MDJiZDMyNjY0NGU0MjJiNj"
        "ZmZjFjMTgxODg0MzU1M2UwMzBmZGU1YjIxYzRiMjVlMWZiZjRjODdkOGRmNjI3OGY4NmExZjRjNDFmMTA0YjQ0MzNk"
        "NGViYWMzNWI0YTI4YiIsInByX2hfdyI6ICIxOTFlNjFjZDQ1OWZiMWVkMGVjYmRjZTdiMjMwMTc4OTY4Y2IwZmFjZm"
        "Y0MzA5Y2NiOGRjYTg2NGEyM2IwOTI4M2Y0NjgxZTQ3MTM3NzFiOTVhMmFkYWRhZWE5Yjc4MTVhNjhjNDk4NmI2MmE2"
        "ZGY3YmNkMzI3NzRiYTI0OTJjM2U5MmViOGZhNmVkZDVmNWIxZjY5N2IxZGRmY2Q5OWIxYzg3MDU1MDM4YmJhYjkyYz"
        "YwMDMxNGM1MmI5YzRkOGU0OGVjMmIzM2ZjYzk3Y2ViMzg3Mzk2ZWQ0NDA5MzZiYWNlMjFjZDUwYzg0NjllY2U1MDY4"
        "NmE1NGFmYjE1M2QxYzIwMCIsInUiOiAiMjYwZDkwNWFmZDQ2ZjRlYzY5YjZhMTYwNDE0OTUzMDM5OTE0NmNkYWVkZW"
        "QxNDdkNzY2YjgwZDNiNzBhYzliODlmYTZiM2RmZDM1ZGQxMTEzNTcxYTc2NjIzNjdmYTQxNWQwMjVhNzdlNTVkZTdi"
        "YmI2YjhmNzYwN2E4ZDhlNGI0MjA2ZmY0YzM5MjgzNmRkMjZhZWFiZGYzMjg1OTQ5MmM4NjU0ODM5ZGQ3MThjZTBkYj"
        "U3OTEyYTBmNTQzNGIwYjQ0MDY1MGZmNDY1ZjVjMGIyZjg0Y2M0ZDA5ZTlkYTMyY2VlZWU4YjgwNTMyYjM2ODkyYmRj"
        "MWNhMjE2ZGJkNDYyODgiLCJ2IjogIjM4ZTZhNDZmNzYwZTA3OGM4OTYzODgwYjk1MTMyYjdmODQ1YjliM2M0OTU2Mm"
        "FjZjRiZjY4NDIwNDI2YzdjYzVmYjhkMjQxYTc4ZjA4YWJjMWUxOTgzZDg0YTQ5OTY2ZjFhZjJkZmEzNGY3OTAwMjJl"
        "NjU2MTM4MzI5ZWFmNmEzOGYyNGZjYjIyNzk0YTY4Y2Q0NDA1Zjk4YzIzZjYxNmEwZWRiYWY5MDYwOTZlNmRjNTEzMT"
        "k4MzA5NmE1Y2FkOGY2NGI5OTdlMjM3ODU1NmRlM2UyNmFiYzdkY2UzNzQ1ZTg3NDEwM2I5MDgxNzgyMzA0NzE5OWQ4"
        "MWJmMGE1YzY0OTRmIiwidyI6ICIzMWZkMWYzMThiZGFhNGI5ODA1MDk2ZDY2MGNlMDgzZjJkZDhlMjg0MmNmOWQ1Mj"
        "IzMzU4M2MyY2NlMGRkNWNmZDFhYWNhZjcxM2RkYTljM2Y4MmI3ODkxODU4ZjcxZGE0YzM5YzQyODFmZjFmZTI1NWRj"
        "ODIxNzZlMjdlMmQzMmQ4Mjc5MTEzODFlZTI4OWJhOGJiM2ZiMmM1N2ZjNmQ0OTI2ZjEyMDNjNDI4ZDQ3ZjE5ZjhmMz"
        "ViOTlmN2Q1ODQxZWViNWU0Yjg5YjhhMTc3NGQ2Yzc2OThlNzNhZmJlMmE5ZGUwNWUwZWY1MTQxZmZhOTEwZjMwZGI3"
        "ZDk1MmQ0NTRhMiJ9";
    std::string paramInfo =
        "dHlwZSBhIHEgODA0NDY4NDc1Nzk2NTU1ODI1OTc0NDQxNDk5ODkyMzUwNzY3NjQ4NzYxOTQ5MjM1NTQzNjAyNjYzND"
        "EzNjg2NjIzMzA1ODQxODA0NDEyODE4NjA4MTEyNDU3ODkwMDE0MjA1NjYxNDAxOTExNDkxMTg5MTYzMDUxMjI1MjMy"
        "OTY4NzE2Nzk0MTk2Nzg2MDE4NjgyNjY3MDA4MDU5IGggNjAgciAxMzQwNzgwNzkyOTk0MjU5NzA5OTU3NDAyNDk5OD"
        "IwNTg0NjEyNzQ3OTM2NTgyMDU5MjM5MzM3NzcyMzU2MTQ0MzcyMTc2NDAzMDA3MzU0Njk3NjgwMTg3NDI5ODE2Njkw"
        "MzQyNzY5MDAzMTg1ODE4NjQ4NjA1MDg1Mzc1Mzg4MjgxMTk0NjU2OTk0NjQzMzY0NDcxMTExNjgwMSBleHAyIDUxMi"
        "BleHAxIDMyIHNpZ24xIC0xIHNpZ24wIDE=";
    dev::eth::ContractABI abi;
    bytes in = abi.abiIn(
        "groupSigVerify(string,string,string,string)", signature, message1, gpkInfo, paramInfo);
    auto execResult = groupSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = execResult->execResult();

    bool real_result1 = true;
    bool result1;
    abi.abiOut(bytesConstRef(&out), result1);
    BOOST_TEST(real_result1 == result1);

    // false
    std::string message2 = "groupSigVerify";
    in = abi.abiIn(
        "groupSigVerify(string,string,string,string)", signature, message2, gpkInfo, paramInfo);
    execResult = groupSigPrecompiled->call(context, bytesConstRef(&in));
    out = execResult->execResult();
    bool real_result2 = false;
    bool result2;
    abi.abiOut(bytesConstRef(&out), result2);
    BOOST_TEST(real_result2 == result2);
}

BOOST_AUTO_TEST_CASE(ErrorFunc)
{
    dev::eth::ContractABI abi;
    bytes in = abi.abiIn("groupSigVerify(string)", std::string("2AE3FFE2"));
    auto execResult = groupSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = execResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_UNKNOW_FUNCTION_CALL);
    }
    else
    {
        BOOST_TEST(count == -CODE_UNKNOW_FUNCTION_CALL);
    }
}

BOOST_AUTO_TEST_CASE(InvalidInputs)
{
    // situation1
    dev::eth::ContractABI abi;
    bytes in = abi.abiIn("groupSigVerify(string,string,string,string)", std::string("2AE3FFE2"),
        std::string("2AE3FFE2"), std::string("2AE3FFE2"), std::string("2AE3FFE2"));
    auto execResult = groupSigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = execResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == VERIFY_GROUP_SIG_FAILED);
    }
    else
    {
        BOOST_TEST(count == -VERIFY_GROUP_SIG_FAILED);
    }

    // situation2
    std::string signature2 =
        "eyJUMiI6ICIwY2Q5YWEwM2I4MmIyZGI3MDhkYWIxM2Q3MWQwOWEwZmE4ZGVkYzgwNTBhYjIwNGRmMjcwZjBlODUwOG"
        "E4ZTE4ODUyYWE1ZjE1MzYwZGM3YjE4YzVjZDY5ODI3MjhlZmM1YzE1NzIwY2U5MGYzM2NiZjdhZWIzNjM0ZTVkNTZk"
        "MGY3MzM3Y2Y5ZWIzOWVmYmUyM2FmYTIzYzU1NDg3ODBmNmQ1MDM5NTc4YjYxNWI3ZjkwZjk3YjVhMWU4ZjdmN2YxN2"
        "M0MzY2YjJkYWFjMDg3ZWY3OGI2N2JmYTlmYzAwY2RkYTAzNGNiNDkyMzIxM2JhNTYzMjZlM2VlMzlmYWMzNzVjNSIs"
        "IlQzIjogIjBhMGIyYTU3OTYzZTFmZjBjYmY2ZmY0MjI0NjAxZjE2ODMwNmViNWZlNTE2NTQzMzJiMWI1OTdlMWIwNW"
        "RkMTNjY2Y2NjJiZDdlYTFjY2UzY2JkNTEzNTMwZDc1YWNmZjFjMGNiYTM4NzgyNmE5NTQ5NDA2ZWNhZjYzNDNhMGJm"
        "NjkyNjM5MmQ1MDBhNDc0YWQ0Nzc0MDFjYTI5YmZmZjgyYWI3OTQyZDIyZTcwZDhiZmU1MzBlMTM0NTFmOGFkYzE4YT"
        "k0MDliOTQyM2U4OTc4MzhmYzY5NzBkMjE2MmE4NzhlNmI5MWE5YWVjOTc5NzFjZTJkMTk1MWEyYjk4MzcwMGUwIn0"
        "=";
    std::string message2 = "ringSigVerify";
    std::string gpkInfo2 =
        "eyJnMSI6ICIxZmNkZWQ1ZGU0Zjc3ZTQ2MWE4N2Q0ZmY4ZmY5YjI1ZDQ5MzkxYjY2Y2MyNGUxN2E2NjYxODRhZDBhYm"
        "IzZDcwYTc4MDgwNTNjMjAxODhiZjA0NjBiZTA1YjQ0YzEyYmFhMzVhNjUxM2YwM2ZlZTVjNjU3ZmRlYzA1MDBlNGVj"
        "YTY1MTcyYzI2MmNiNzExMGNhZGE5ZDcyZTNmYmFiZTcxYThjOWFjNDZiMTA5MTVjNGIzNGZmZjQyOWVjMGJiNjU0ZD"
        "Y3YzVlNjA2NzI3YjIxM2ZhY2JkMDA1N2ZjYjE3ZWYyM2QwOTNkNGY2ZmUxZjk2NmMzYmFjMjk4NjI4ZTJjZGMwMyIs"
        "ImcyIjogIjI2MDhlZDNlODU3ZmFlZTllZDRiNjVkZjY5MTQzMzdlOTg5N2Q1Mjc1NGJlODg2ZDc4ZDFiMWFiMGQxMT"
        "czZTQyNjBlYWVlOTg5YjY3MjdmZDFhNDdmMGE1Y2M1M2IyNDY3NzBhYTcxOGIyOGI1NmJhODJjZDNhM2M2NzgxMzkw"
        "YWQzMjU2NGVhMzU3MDFmZWViOTExZWQzNzA5YjQ1MmI2N2RiMjhiNmUxZmZmZWZiZTE5Y2M2NzBkOTRkN2NhMDZiMm"
        "RlNmU5YzVmYWI2ZjMxMjE0Njk2NjRlYWZkNWNkZWNlNmZhMGU3ZTE0MjEyMTY3OTE5MDI1N2JjMWE4MDc5M2VjIn0"
        "=";
    std::string paramInfo2 =
        "dHlwZSBhODA0NDY4NDc1Nzk2NTU1ODI1OTc0NDQxNDk5ODkyMzUwNzY3NjQ4NzYxOTQ5MjM1NTQzNjAyNjYzNDEzNj"
        "g2NjIzMzA1ODQxODA0NDEyODE4NjA4MTEyNDU3ODkwMDE0MjA1NjYxNDAxOTExNDkxMTg5MTYzMDUxMjI1MjMyOTY4"
        "NzE2Nzk0MTk2Nzg2MDE4NjgyNjY3MDA4MDU5IGggNjAgciAxMzQwNzgwNzkyOTk0MjU5NzA5OTU3NDAyNDk5ODIwNT"
        "g0NjEyNzQ3OTM2NTgyMDU5MjM5MzM3NzcyMzU2MTQ0MzcyMTc2NDAzMDA3MzU0Njk3NjgwMTg3NDI5ODE2NjkwMzQy"
        "NzY5MDAzMTg1ODE4NjQ4NjA1MDg1Mzc1Mzg4MjgxMTk0NjU2OTk0NjQzMzY0NDcxMTExNjgwMSBleHAyIDUxMiBleH"
        "AxIDMyIHNpZ24xIC0xIHNpZ24wIDE=";
    in = abi.abiIn(
        "groupSigVerify(string,string,string,string)", signature2, message2, gpkInfo2, paramInfo2);
    execResult = groupSigPrecompiled->call(context, bytesConstRef(&in));
    out = execResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == VERIFY_GROUP_SIG_FAILED);
    }
    else
    {
        BOOST_TEST(count == -VERIFY_GROUP_SIG_FAILED);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_GroupSigPrecompiled
