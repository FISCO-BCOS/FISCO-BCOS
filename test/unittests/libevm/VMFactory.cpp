/**
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
 *
 * @brief
 *
 * @file VMFactory.cpp
 * @author: chaychen
 * @date 2018-09-05
 */
#include "FakeExtVMFace.h"
#include "libdevcrypto/CryptoInterface.h"
#include <libevm/EVMC.h>
#include <libevm/VMFace.h>
#include <libevm/VMFactory.h>
#include <libinterpreter/interpreter.h>
#include <test/tools/libbcos/Options.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(VMFaceTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testInterpreterEvmC)
{
    std::string code_str = "ExtVMFace Test";
    bytes code(code_str.begin(), code_str.end());
    u256 gasUsed = u256(300000);
    u256 gasLimit = u256(300000);
    EnvInfo env_info = InitEnvInfo::createEnvInfo(gasUsed, gasLimit);
    CallParameters param = InitCallParams::createRandomCallParams();
    FakeExtVM fake_ext_vm(env_info, param.codeAddress, param.senderAddress, param.senderAddress,
        param.valueTransfer, param.gas, param.data, code, crypto::Hash(code_str), 0, false, true);
    std::unique_ptr<VMFace> m_face = VMFactory::create(VMKind::Interpreter);
    u256 io_gas = u256(200000);
    BOOST_CHECK_NO_THROW(m_face->exec(io_gas, fake_ext_vm, OnOpFunc{}));
}

BOOST_AUTO_TEST_CASE(testVMOptionParser)
{
    const int argc = 2;
    char** argv = new char*[argc];
    const char* arg1 = "--vm=interpreter";
    const char* arg2 = "--evmc =evmc_flag=test";
    argv[0] = new char[strlen(arg1) + 1];
    argv[1] = new char[strlen(arg2) + 1];
    strcpy(argv[0], arg1);
    strcpy(argv[1], arg2);
    BOOST_CHECK(Options::get(argc, argv).vm_name == "interpreter");
    BOOST_CHECK(Options::get(argc, argv).evmc_options.size() == 0);
    delete[] argv[0];
    delete[] argv[1];
    delete[] argv;
}

BOOST_AUTO_TEST_CASE(testToRevision)
{
    EVMSchedule schedule;
    schedule.haveDelegateCall = false;
    BOOST_CHECK(EVMC_FRONTIER == toRevision(schedule));
    schedule.haveDelegateCall = true;
    BOOST_CHECK(EVMC_HOMESTEAD == toRevision(schedule));
    schedule.eip150Mode = true;
    BOOST_CHECK(EVMC_TANGERINE_WHISTLE == toRevision(schedule));
    schedule.eip158Mode = true;
    BOOST_CHECK(EVMC_SPURIOUS_DRAGON == toRevision(schedule));
    schedule.haveRevert = true;
    BOOST_CHECK(EVMC_BYZANTIUM == toRevision(schedule));
    schedule.haveCreate2 = true;
    BOOST_CHECK(EVMC_CONSTANTINOPLE == toRevision(schedule));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
