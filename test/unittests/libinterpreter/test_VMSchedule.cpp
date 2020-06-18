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
 * (c) 2016-2020 fisco-dev contributors.
 *
 * @brief: unit test for VMSchedule
 *
 * @file test_VMSchedule.cpp
 * @author: yujiechen
 * @date 20200410
 */
#include <libethcore/EVMFlags.h>
// #include <libinterpreter/VM.h>
// #include <libinterpreter/VMSchedule.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(VMScheduleTest, TestOutputHelperFixture)
#if 0
std::shared_ptr<VM> createAndInitVMSchedule(VMFlagType const& _vmFlag)
{
    auto vm = std::make_shared<VM>();
    evmc_context* context = new evmc_context;
    context->flags = _vmFlag;
    vm->createVMSchedule(context);
    delete context;
    return vm;
}

void checkBasicCost(VMSchedule::Ptr _schedule)
{
    BOOST_CHECK(_schedule->stepGas0 == 0);
    BOOST_CHECK(_schedule->stepGas1 == 2);
    BOOST_CHECK(_schedule->stepGas2 == 3);
    BOOST_CHECK(_schedule->stepGas3 == 5);
    BOOST_CHECK(_schedule->stepGas4 == 8);
    BOOST_CHECK(_schedule->stepGas5 == 10);
    BOOST_CHECK(_schedule->stepGas6 == 20);
    BOOST_CHECK(_schedule->sha3Gas == 30);
    BOOST_CHECK(_schedule->sha3WordGas == 6);

    BOOST_CHECK(_schedule->jumpdestGas == 1);
    BOOST_CHECK(_schedule->logGas == 375);
    BOOST_CHECK(_schedule->logDataGas == 8);
    BOOST_CHECK(_schedule->logTopicGas == 375);
    BOOST_CHECK(_schedule->memoryGas == 3);
    BOOST_CHECK(_schedule->quadCoeffDiv == 512);
    BOOST_CHECK(_schedule->copyGas == 3);
}
#endif

BOOST_AUTO_TEST_CASE(testBasicVMSchedule)
{
#if 0
    auto vm = createAndInitVMSchedule(0);
    auto evmSchedule = vm->vmSchedule();
    // check gasCost for evmSchedule
    checkBasicCost(evmSchedule);
    BOOST_CHECK(evmSchedule->sloadGas == 200);
    BOOST_CHECK(evmSchedule->sstoreSetGas == 20000);
    BOOST_CHECK(evmSchedule->sstoreResetGas == 5000);
    BOOST_CHECK(evmSchedule->createGas == 32000);
    BOOST_CHECK(evmSchedule->valueTransferGas == 9000);
    BOOST_CHECK(evmSchedule->callStipend == 2300);
    BOOST_CHECK(evmSchedule->callNewAccount == 25000);
#endif
}

BOOST_AUTO_TEST_CASE(testFreeStorageVMSchedule)
{
#if 0
    VMFlagType vmFlag = 0;
    vmFlag |= EVMFlags::FreeStorageGas;
    auto vm = createAndInitVMSchedule(vmFlag);
    auto evmSchedule = vm->vmSchedule();
    // check gasCost for evmSchedule
    checkBasicCost(evmSchedule);
    BOOST_CHECK(evmSchedule->createGas == 16000);
    BOOST_CHECK(evmSchedule->sstoreSetGas == 1200);
    BOOST_CHECK(evmSchedule->sstoreResetGas == 1200);
    BOOST_CHECK(evmSchedule->sloadGas == 1200);
    BOOST_CHECK(evmSchedule->valueTransferGas == 0);
    BOOST_CHECK(evmSchedule->callStipend == 10);
    BOOST_CHECK(evmSchedule->callNewAccount == 10);
#endif
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
