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
#include <iostream>
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
class InterpreterFixture : TestOutputHelperFixture
{
public:
    InterpreterFixture() : evmc(evmc_create_interpreter()){};

    FakeEvmc evmc;
    FakeState& state = evmc.getState();

    u256 getStateValue(Address account, string const& key)
    {
        return fromEvmC(state[account.hex()][key]);
    }

    void printResult(evmc_result const& r)
    {
        cout << "status_code: " << r.status_code << endl;
        cout << "gas_left: " << r.gas_left << endl;
        cout << "create_address: " << fromEvmC(r.create_address).hex() << endl;
        cout << "output_size: " << r.output_size << endl;
        cout << "output_data: " << endl;
        cout << "------ begin -------" << endl;
        for (size_t i = 0; i < r.output_size; i++)
            cout << hex << setw(2) << setfill('0') << (int)(r.output_data[i])
                 << ((i + 1) % 32 == 0 && (i + 1) != r.output_size ? "\n" : "");
        cout << endl << dec;
        cout << "------  end  -------" << endl;
    }

    void printAccount(Address const& addr)
    {
        cout << "account data:" << endl;
        state.printAccountAllData(toEvmC(addr));
    }
};

BOOST_FIXTURE_TEST_SUITE(InterpreterTest, InterpreterFixture)

BOOST_AUTO_TEST_CASE(addTest)
{
    // To verify 1 + 2 == 3
    // PUSH1 20 PUSH1 00 --> RETUEN[size, begin]
    // PUSH1 01 PUSH1 02
    // ADD
    // PUSH1 00
    // MSTORE
    // RETURN

    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex("602060006001600201600052f3");
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = false;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);

    u256 r = 0;
    for (size_t i = 0; i < 32; i++)
        r = (r << 8) | result.output_data[i];

    BOOST_CHECK(u256(3) == r);
}

BOOST_AUTO_TEST_CASE(contractDeployTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
    }
    */

    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        "60606040523415600b57fe5b5b60338060196000396000f30060606040525bfe00a165627a7a72305820de136e"
        "86e236113a9f32948ce4a57e1f7a409db615e7ef07a26ef1ebc39de3580029");
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);

    bytes compare = fromHex(
        "60606040525bfe00a165627a7a72305820de136e86e236113a9f32948ce4a57e1f7a409db615e7ef07a26ef1eb"
        "c39de3580029");
    bytes codeRes = bytesConstRef(result.output_data, result.output_size).toVector();

    BOOST_CHECK(compare == codeRes);
}

BOOST_AUTO_TEST_CASE(contractConstructorTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
    uint256 a;
    uint256 b;
    function C(uint256 _a) {
        a = _a;
        b = _a;
    }
    }
    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string(
            "60606040523415600b57fe5b6040516020806078833981016040528080519060200190919050505b806000"
            "81905550806001819055505b505b60338060456000396000f30060606040525bfe00a165627a7a72305820"
            "4a8d7ec58458a207cc1e6ab502444332fe0648d29f307852289dbc1b87b07d510029") +
        string("0000000000000000000000000000000000000000000000000000000000000042"));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);

    u256 ra = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 rb = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");

    BOOST_CHECK_EQUAL(u256(66), ra);
    BOOST_CHECK_EQUAL(u256(66), rb);
}

BOOST_AUTO_TEST_CASE(arithmeticCaculateTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        uint256 x1;
        uint256 x2;
        uint256 x3;
        uint256 x4;
        uint256 x5;
        function C(uint256 _a) {
            x1 = _a + 1;
            x2 = _a - 1;
            x3 = _a * 100;
            x4 = _a / 3;
            x5 = _a % 10;
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code =
        fromHex(string("60606040523415600b57fe5b60405160208060ac83398101604052808051906020019091905"
                       "0505b6001810160008190555060018103600181905550606481026002819055506003818115"
                       "15605057fe5b04600381905550600a81811515606257fe5b066004819055505b505b6033806"
                       "0796000396000f30060606040525bfe00a165627a7a72305820e4c4dc7f619bccec3502825e"
                       "66d98c0b237524e4f673954608f8578e921ebb080029") +
                string("0000000000000000000000000000000000000000000000000000000000000042"));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);

    u256 x1 = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 x2 = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");
    u256 x3 = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000002");
    u256 x4 = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000003");
    u256 x5 = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000004");

    BOOST_CHECK_EQUAL(u256(66 + 1), x1);
    BOOST_CHECK_EQUAL(u256(66 - 1), x2);
    BOOST_CHECK_EQUAL(u256(66 * 100), x3);
    BOOST_CHECK_EQUAL(u256(66 / 3), x4);
    BOOST_CHECK_EQUAL(u256(66 % 10), x5);
}

BOOST_AUTO_TEST_CASE(logicalCaculateTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        bool x1;
        bool x2;
        bool x3;
        bool x4;
        bool x5;
        function C(uint256 _a) {
            uint256 small = _a;
            uint256 big = _a + 1;
            x1 = small < big;
            x2 = small <= big;
            x3 = small == big;
            x4 = small > big;
            x5 = small >= big;
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("60606040523415600b57fe5b60405160208061010c833981016040528080519060200190919050505b6"
               "0006000829150600183019050808210600060006101000a81548160ff02191690831515021790555080"
               "821115600060016101000a81548160ff02191690831515021790555080821415600060026101000a815"
               "48160ff02191690831515021790555080821115600060036101000a81548160ff021916908315150217"
               "9055508082101515600060046101000a81548160ff0219169083151502179055505b5050505b6033806"
               "100d96000396000f30060606040525bfe00a165627a7a7230582074f82852a7899b3b0a67289c346f50"
               "f08b2eca7ab65580af33dedb3e72b68bde0029") +
        string("0000000000000000000000000000000000000000000000000000000000000042"));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);

    // EVM combines all true variable together!
    u256 xall = getStateValue(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");

    // 5 true variables from x1 to x5: 01 01 01 01 01 is combined together.
    BOOST_CHECK_EQUAL(u256(0x0101010101), xall);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
