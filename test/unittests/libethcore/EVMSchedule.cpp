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
 * @brief : unit tests for EVMSchedule of libethcore
 *
 * @file EVMSchedule.cpp
 * @author: yujiechen
 * @date 2018-09-02
 */


#include <libethcore/EVMSchedule.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EVMSchedulerTest, TestOutputHelperFixture)

void testHomesteadScheduleCase()
{
    BOOST_CHECK(HomesteadSchedule.exceptionalFailedCodeDeposit == true);
    BOOST_CHECK(HomesteadSchedule.haveDelegateCall == true);
    BOOST_CHECK(HomesteadSchedule.txCreateGas == 53000);
}

void testEIP150ScheduleCase()
{
    testHomesteadScheduleCase();
    /// test other features
    BOOST_CHECK(EIP150Schedule.eip150Mode == true);
    BOOST_CHECK(EIP150Schedule.extcodesizeGas == 700);
    BOOST_CHECK(EIP150Schedule.extcodecopyGas == 700);
    BOOST_CHECK(EIP150Schedule.balanceGas == 400);
    BOOST_CHECK(EIP150Schedule.sloadGas == 200);
    BOOST_CHECK(EIP150Schedule.callGas == 700);
    BOOST_CHECK(EIP150Schedule.suicideGas == 5000);
}

void testEIP158ScheduleCase()
{
    testEIP150ScheduleCase();
    BOOST_CHECK(EIP158Schedule.expByteGas == 50);
    BOOST_CHECK(EIP158Schedule.eip158Mode == true);
    BOOST_CHECK(EIP158Schedule.maxCodeSize == 0x6000);
}

void testByzantiumScheduleCase()
{
    testEIP158ScheduleCase();
    BOOST_CHECK(ByzantiumSchedule.haveRevert == true);
    BOOST_CHECK(ByzantiumSchedule.haveReturnData == true);
    BOOST_CHECK(ByzantiumSchedule.haveStaticCall == true);
}

void testEWASMScheduleCase()
{
    testByzantiumScheduleCase();
    BOOST_CHECK(EWASMSchedule.maxCodeSize == std::numeric_limits<unsigned>::max());
    BOOST_CHECK(EWASMSchedule.txDataZeroGas == EWASMSchedule.txDataNonZeroGas);
}

void testConstantinopleScheduleCase()
{
    testByzantiumScheduleCase();
    BOOST_CHECK(ConstantinopleSchedule.blockhashGas == 800);
    BOOST_CHECK(ConstantinopleSchedule.haveCreate2 == true);
    BOOST_CHECK(ConstantinopleSchedule.haveBitwiseShifting == true);
    BOOST_CHECK(ConstantinopleSchedule.haveExtcodehash == true);
}

void testFiscoBcosScheduleCase()
{
    testConstantinopleScheduleCase();
}

void testExperimentalScheduleCase()
{
    testConstantinopleScheduleCase();
}

/// test "FrontierSchedule"
BOOST_AUTO_TEST_CASE(testFrontierSchedule)
{
    BOOST_CHECK(FrontierSchedule.exceptionalFailedCodeDeposit == false);
    BOOST_CHECK(FrontierSchedule.haveDelegateCall == false);
    BOOST_CHECK(FrontierSchedule.txCreateGas == 21000);
}
BOOST_AUTO_TEST_CASE(testHomesteadSchedule)
{
    testHomesteadScheduleCase();
}
BOOST_AUTO_TEST_CASE(testEIP150Schedule)
{
    testEIP150ScheduleCase();
}

BOOST_AUTO_TEST_CASE(testEIP158Schedule)
{
    testEIP158ScheduleCase();
}
BOOST_AUTO_TEST_CASE(testByzantiumSchedule)
{
    testByzantiumScheduleCase();
}
BOOST_AUTO_TEST_CASE(testEWASMSchedule)
{
    testEWASMScheduleCase();
}
BOOST_AUTO_TEST_CASE(testConstantinopleSchedule)
{
    testConstantinopleScheduleCase();
}
BOOST_AUTO_TEST_CASE(ExperimentalSchedule)
{
    testExperimentalScheduleCase();
}
BOOST_AUTO_TEST_CASE(FiscoBcosSchedule)
{
    testFiscoBcosScheduleCase();
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
