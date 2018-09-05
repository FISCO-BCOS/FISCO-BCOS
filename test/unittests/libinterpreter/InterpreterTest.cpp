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
 * @file InterpreterTest.cpp
 * @author: jimmyshi
 * @date 2018-09-04
 */

#include <evmc/evmc.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/SHA3.h>
#include <libdevcrypto/Common.h>
#include <libethcore/EVMSchedule.h>
#include <libinterpreter/interpreter.h>
#include <test/tools/libutils/FakeEvmc.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <memory>


using namespace std;
using namespace dev;
using namespace dev::test;
using namespace dev::eth;

namespace dev
{
namespace test
{
FakeEvmc evmc(evmc_create_interpreter());

BOOST_FIXTURE_TEST_SUITE(InterpreterTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(demoTest)
{
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex("368060006000376101238160006000f55050");
    bytes data = fromHex("606060005260206000f3");
    Address myAddress{KeyPair::create().address()};
    Address caller = myAddress;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = false;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, myAddress, caller, value, gas, depth, isCreate, isStaticCall);
    // BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    std::cout << result.status_code << std::endl;
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
