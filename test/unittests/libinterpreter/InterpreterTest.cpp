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

    u256 getStateValueU256(Address account, string const& key)
    {
        return fromBigEndian<u256>(state[account.hex()][key].bytes);
    }

    s256 getStateValueS256(Address account, string const& key)
    {
        return u2s(fromBigEndian<u256>(state[account.hex()][key].bytes));
    }

    Address getStateValueAddress(Address account, string const& key)
    {
        evmc_uint256be& value = state[account.hex()][key];
        evmc_address addr;

        std::memcpy(addr.bytes, value.bytes + 12, 20);
        return reinterpret_cast<Address const&>(addr);
    }

    void setStateAccountBalance(Address account, u256 balance)
    {
        state.accountBalance(toEvmC(account)) = toEvmC(balance);
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
    BOOST_CHECK(0 == result.status_code);
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
    BOOST_CHECK(0 == result.status_code);
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

    u256 ra = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 rb = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");

    BOOST_CHECK_EQUAL(u256(66), ra);
    BOOST_CHECK_EQUAL(u256(66), rb);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(arithmeticCaculateTest1)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        uint256 x1;
        uint256 x2;
        uint256 x3;
        uint256 x4;
        uint256 x5;
        uint256 x6;
        uint256 x7;
        uint256 x8;
        function C(uint256 _a) {
            x1 = _a + 1;
            x2 = _a - 1;
            x3 = _a * 100;
            x4 = _a / 3;
            x5 = _a % 10;
            x6 = _a ** 2;
            x7 = _a << 3;
            x8 = _a >> 1;
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string(
            "60606040523415600b57fe5b60405160208060d3833981016040528080519060200190919050505b600181"
            "016000819055506001810360018190555060648102600281905550600381811515605057fe5b0460038190"
            "5550600a81811515606257fe5b066004819055506002810a6005819055506003819060020a026006819055"
            "506001819060020a90046007819055505b505b60338060a06000396000f30060606040525bfe00a165627a"
            "7a7230582089c2307e810ceda56160585b80561a69fc72cb46a335d905cd748801c9ba2d830029") +
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

    u256 x1 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 x2 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");
    u256 x3 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000002");
    u256 x4 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000003");
    u256 x5 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000004");
    u256 x6 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000005");
    u256 x7 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000006");
    u256 x8 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000007");

    BOOST_CHECK_EQUAL(u256(66 + 1), x1);
    BOOST_CHECK_EQUAL(u256(66 - 1), x2);
    BOOST_CHECK_EQUAL(u256(66 * 100), x3);
    BOOST_CHECK_EQUAL(u256(66 / 3), x4);
    BOOST_CHECK_EQUAL(u256(66 % 10), x5);
    BOOST_CHECK_EQUAL(u256(66 * 66), x6);
    BOOST_CHECK_EQUAL(u256(66 << 3), x7);
    BOOST_CHECK_EQUAL(u256(66 >> 1), x8);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(arithmeticCaculateTest2)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        int256 x1;
        int256 x2;
        int256 x3;
        int256 x4;
        int256 x5;
        int256 x6;
        int256 x7;
        int256 x8;
        int256 x9;
        int256 x10;
        int256 x11;
        function C(int256 _a) {
            x1 = _a + 1;
            x2 = _a - 100;
            x3 = _a * -100;
            x4 = _a / 3;
            x5 = _a % 10;
            (x6, x7, x8, x9, x10, x11) = asmcall();
        }
        function asmcall() constant returns(int256 y1, int256 y2, int256 y3, int256 y4, int256 y5,
    int256 y6)
        {
            assembly{
                y1 := shl(2, 4)
                y2 := shr(2, 8)
                y3 := sar(1, 4)
                y4 := addmod(10, 7, 6)
                y5 := mulmod(3, 4, 7)
                y6 := byte(31, 0xff)
            }
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b5060405160208061023683398101806040528101908080519"
               "06020019092919050505060018101600081905550606481036001819055507fffffffffffffffffffff"
               "ffffffffffffffffffffffffffffffffffffffffff9c810260028190555060038181151561007c57fe5"
               "b05600381905550600a8181151561008f57fe5b076004819055506100ad6100ef640100000000026401"
               "000000009004565b6005600060066000600760006008600060096000600a60008c919050558b9190505"
               "58a919050558991905055889190505587919050555050505050505061012e565b600080600080600080"
               "600460021b9550600860021c9450600460011d935060066007600a08925060076004600309915060ff6"
               "01f1a9050909192939495565b60fa8061013c6000396000f300608060405260043610603f576000357c"
               "0100000000000000000000000000000000000000000000000000000000900463ffffffff168063035ce"
               "6fa146044575b600080fd5b348015604f57600080fd5b506056608f565b604051808781526020018681"
               "52602001858152602001848152602001838152602001828152602001965050505050505060405180910"
               "390f35b600080600080600080600460021b9550600860021c9450600460011d935060066007600a0892"
               "5060076004600309915060ff601f1a90509091929394955600a165627a7a72305820cc4f9b8d08e25bd"
               "e5e4071499ce01359a27bd898c321c7c0150eac91b3a38aa50029") +
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

    s256 x1 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    s256 x2 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");
    s256 x3 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000002");
    s256 x4 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000003");
    s256 x5 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000004");
    s256 x6 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000005");
    s256 x7 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000006");
    s256 x8 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000007");
    s256 x9 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000008");
    s256 x10 = getStateValueS256(
        destination, "0000000000000000000000000000000000000000000000000000000000000009");
    s256 x11 = getStateValueS256(
        destination, "000000000000000000000000000000000000000000000000000000000000000a");


    BOOST_CHECK_EQUAL(s256(66 + 1), x1);
    BOOST_CHECK_EQUAL(s256(66 - 100), x2);
    BOOST_CHECK_EQUAL(s256(66 * (-100)), x3);
    BOOST_CHECK_EQUAL(s256(66 / 3), x4);
    BOOST_CHECK_EQUAL(s256(66 % 10), x5);
    BOOST_CHECK_EQUAL(s256(4 << 2), x6);
    BOOST_CHECK_EQUAL(s256(8 >> 2), x7);
    BOOST_CHECK_EQUAL(s256(4 >> 1), x8);
    BOOST_CHECK_EQUAL(s256((10 + 7) % 6), x9);
    BOOST_CHECK_EQUAL(s256((3 * 4) % 7), x10);
    BOOST_CHECK_EQUAL(s256(0xff), x11);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(comparisonsTest1)
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
            x3 = !(small == big);
            x4 = !(small > big);
            x5 = !(small >= big);
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
    u256 xall = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");

    // 5 true variables from x1 to x5: 01 01 01 01 01 is combined together.
    BOOST_CHECK_EQUAL(u256(0x0101010101), xall);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(comparisonsTest2)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        bool x1;
        bool x2;
        bool x3;
        bool x4;
        bool x5;
        function C(int256 _a) {
            int256 small = _a;
            int256 big = _a + 1;
            x1 = small < big;
            x2 = small <= big;
            x3 = !(small == big);
            x4 = !(small > big);
            x5 = !(small >= big);
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("60606040523415600b57fe5b60405160208060c283398101604052515b6000805460ff1916600183018"
               "0841291821761ff001916610100828613159081029190911762ff000019168286141562010000021763"
               "ff00000019166301000000919091021764ff00000000191664010000000092909202919091179091558"
               "1905b5050505b603380608f6000396000f30060606040525bfe00a165627a7a723058207ffb02d83832"
               "bb77d2f385eac28618b0d66b382244251d54fec46642f8252eeb0029") +
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
    u256 xall = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");

    // 5 true variables from x1 to x5: 01 01 01 01 01 is combined together.
    BOOST_CHECK_EQUAL(u256(0x0101010101), xall);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(bitOperationTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        uint256 x1;
        uint256 x2;
        uint256 x3;
        uint256 x4;
        function C(uint256 _a) {
            x1 = _a & 0x0101;
            x2 = _a | 0x0101;
            x3 = _a ^ 0x0101;
            x4 = ~_a ;
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("60606040523415600b57fe5b604051602080607883398101604052515b6101018082166000558082176"
               "00155811860025580196003555b505b60338060456000396000f30060606040525bfe00a165627a7a72"
               "305820defb2016449ae0c78dc39227ce845b9b0ca1f975a4dd3bcf448f0296b9ac2e920029") +
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

    u256 x1 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 x2 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");
    u256 x3 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000002");
    u256 x4 = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000003");

    BOOST_CHECK_EQUAL(u256(66 & 0x0101), x1);
    BOOST_CHECK_EQUAL(u256(66 | 0x0101), x2);
    BOOST_CHECK_EQUAL(u256(66 ^ 0x0101), x3);
    BOOST_CHECK_EQUAL(~u256(66), x4);
    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(contextTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        address orig;
        address dest;
        address caller;
        address cbase;
        uint bn;
        uint diff;
        uint lmt;
        uint stp;
        uint256 gs;
        uint vle;
        uint gpris;
        bytes4 msig;
        bytes dta;
        bytes32 bh;
        function C() payable{
            orig = tx.origin;
            dest = this;
            caller = msg.sender;
            cbase = block.coinbase;
            bn = block.number;
            diff = block.difficulty;
            lmt = block.gaslimit;
            stp = block.timestamp;
            gs = msg.gas;
            vle = msg.value;
            gpris = tx.gasprice;
            msig = msg.sig;
            dta = msg.data;
            bh = block.blockhash(bn - 1);
        }
    }

    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(string(
        "6080604052326000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffff"
        "ffffffffffffffffffffffffffffffffffff16021790555030600160006101000a81548173ffffffffffffffff"
        "ffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555033"
        "600260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffff"
        "ffffffffffffffffffffffffff16021790555041600360006101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550436004819055"
        "504460058190555045600681905550426007819055505a600881905550346009819055503a600a819055506000"
        "357fffffffff0000000000000000000000000000000000000000000000000000000016600b60006101000a8154"
        "8163ffffffff02191690837c010000000000000000000000000000000000000000000000000000000090040217"
        "905550600036600c91906101a99291906101c0565b5060016004540340600d8160001916905550610265565b82"
        "8054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106102"
        "0157803560ff191683800117855561022f565b8280016001018555821561022f579182015b8281111561022e57"
        "8235825591602001919060010190610213565b5b50905061023c9190610240565b5090565b61026291905b8082"
        "111561025e576000816000905550600101610246565b5090565b90565b6035806102736000396000f300608060"
        "4052600080fd00a165627a7a723058204d40cf1e6a3425cc568088e0eaf1e55e129ced45df2b6a0af442d7ec2b"
        "6bfd050029"));
    bytes data = fromHex("jimmyshi");
    Address destination{KeyPair::create().address()};
    Address caller = right160(
        sha3(fromHex("ff") + destination.asBytes() + toBigEndian(0x123_cppui256) + sha3(data)));
    u256 value = 1024;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);

    /*
        address orig;
        address dest;
        address caller;
        address cbase;
        uint bn;
        uint diff;
        uint lmt;
        uint stp;
        uint256 gs;
        uint vle;
        uint gpris;
        bytes4 msig;
        bytes dta;
        bytes32 bh;
    */

    Address originResult = getStateValueAddress(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    Address destResult = getStateValueAddress(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");
    Address callerResult = getStateValueAddress(
        destination, "0000000000000000000000000000000000000000000000000000000000000002");
    Address coinbaseResult = getStateValueAddress(
        destination, "0000000000000000000000000000000000000000000000000000000000000003");

    u256 blocknumberResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000004");
    u256 difficultyResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000005");
    u256 gaslimitResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000006");
    u256 timestampResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000007");
    // u256 gasResult = getStateValueU256(
    // destination, "0000000000000000000000000000000000000000000000000000000000000008");
    u256 valueResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000009");
    u256 gasPriceResult = getStateValueU256(
        destination, "000000000000000000000000000000000000000000000000000000000000000a");
    h256 blockHashResult = FixedHash<32>(getStateValueU256(
        destination, "000000000000000000000000000000000000000000000000000000000000000d"));

    BOOST_CHECK(originResult == FAKE_ORIGIN);
    BOOST_CHECK(destResult == destination);
    BOOST_CHECK(callerResult == caller);
    BOOST_CHECK(coinbaseResult == FAKE_COINBASE);
    BOOST_CHECK(blocknumberResult == u256(FAKE_BLOCK_NUMBER));
    BOOST_CHECK(difficultyResult == FAKE_DIFFICULTY);
    BOOST_CHECK(gaslimitResult == u256(FAKE_GAS_LIMIT));
    BOOST_CHECK(timestampResult == u256(FAKE_TIMESTAMP));
    // BOOST_CHECK(gasResult == );
    BOOST_CHECK(valueResult == value);
    BOOST_CHECK(gasPriceResult == FAKE_GAS_PRICE);
    BOOST_CHECK(blockHashResult == FAKE_BLOCK_HASH);

    BOOST_CHECK(0 == result.status_code);
}


BOOST_AUTO_TEST_CASE(balanceTest)
{
    /*
    pragma solidity ^0.4.11;
    contract C {
        uint bala;
        function C() payable{
            bala = this.balance;
        }
    }
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code =
        fromHex(string("60606040525b3073ffffffffffffffffffffffffffffffffffffffff16316000819055505b5"
                       "b60338060326000396000f30060606040525bfe00a165627a7a723058206e9b4f242a829a77"
                       "e5b3bcee4d2e4fb9beae368d11d3d541423c8f481f7959fb0029") +
                string(""));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = destination;
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;
    u256 balance = 123456;
    setStateAccountBalance(destination, balance);

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);

    BOOST_CHECK_EQUAL(getStateValueU256(destination, "balance"), balance);
    BOOST_CHECK(0 == result.status_code);
}


BOOST_AUTO_TEST_CASE(LogTest)
{
    // TODO: To call LOG0
    /*
    pragma solidity ^0.4.11;
    contract C {
        event Zero();
        event One(uint256 a);
        event Two(uint256 indexed a, uint256 b);
        event Three(uint256 indexed  a, uint256 indexed b, uint256 c);
        event Four(uint256 indexed a, uint256 indexed b, uint256 indexed c, uint256 d);
        function C() {
            Zero();
            One(1);
            Two(1, 2);
            Three(1, 2, 3);
            Four(1, 2, 3, 4);
        }
    }
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("6060604052341561000c57fe5b5b7ff4560403888256621a13abd81f4930ecb469c609f6317e1dd2ee3"
               "4fe69293e1b60405180905060405180910390a17f0b24d1aaf91c6017a8257e85108e3ce49d5461be0d"
               "d82383c5eb5f3eb47a1b4e60016040518082815260200191505060405180910390a160017fce34f015a"
               "0e20f2b0daf980b28ed50729e87b993e4d30cca4c3f4da05acbd0ac6002604051808281526020019150"
               "5060405180910390a2600260017fd3d2405dd719b971a7f9b4e0a312db1f22d361f171f71c9870c7158"
               "33392b52b60036040518082815260200191505060405180910390a36003600260017fc976bb9064fc5b"
               "b5ef2b52e9809965f4a1bb771fac31a4937d151ca668c8c63c600460405180828152602001915050604"
               "05180910390a45b5b6033806101386000396000f30060606040525bfe00a165627a7a723058207517a3"
               "9f110484c49f94079c16e3775ecbe1a3c4cd46afac460426f8426fc4270029") +
        string(""));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = Address("1000000000000000000000000000000000000000");
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);
    BOOST_CHECK_EQUAL(5, fakeLogs.size());

    BOOST_CHECK_EQUAL(destination, fakeLogs[0].address);
    BOOST_CHECK_EQUAL(destination, fakeLogs[1].address);
    BOOST_CHECK_EQUAL(destination, fakeLogs[2].address);
    BOOST_CHECK_EQUAL(destination, fakeLogs[3].address);
    BOOST_CHECK_EQUAL(destination, fakeLogs[4].address);
    // TODO: To call LOG0
    BOOST_CHECK_EQUAL(1, fakeLogs[1].topics.size());
    BOOST_CHECK_EQUAL(2, fakeLogs[2].topics.size());
    BOOST_CHECK_EQUAL(3, fakeLogs[3].topics.size());
    BOOST_CHECK_EQUAL(4, fakeLogs[4].topics.size());

    BOOST_CHECK(0 == result.status_code);
}

BOOST_AUTO_TEST_CASE(accessFunctionTest)
{
    /*
    pragma solidity ^0.4.2;
    contract HelloWorld{
        uint256 x;
        function HelloWorld(){
           x = 123;
        }
        function get()constant returns(uint256){
            return x;
        }
        function set(uint256 n){
            x = n;
        }
    }
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b60008190555060df806100276000396000f30060806"
               "04052600436106049576000357c01000000000000000000000000000000000000000000000000000000"
               "00900463ffffffff16806360fe47b114604e5780636d4ce63c146078575b600080fd5b3480156059576"
               "00080fd5b5060766004803603810190808035906020019092919050505060a0565b005b348015608357"
               "600080fd5b50608a60aa565b6040518082815260200191505060405180910390f35b806000819055505"
               "0565b600080549050905600a165627a7a723058206ed282a30254e86080aeca513abfda612e92567697"
               "0bdf4f17a8e5d36bcd8d230029") +
        string(""));
    bytes data = fromHex("");
    Address destination{KeyPair::create().address()};
    Address caller = Address("1000000000000000000000000000000000000000");
    u256 value = 0;
    int64_t gas = 1000000;
    int32_t depth = 0;
    bool isCreate = true;
    bool isStaticCall = false;

    evmc_result result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);
    BOOST_CHECK(0 == result.status_code);

    // call function get()
    code = fromHex(
        "6080604052600436106049576000357c0100000000000000000000000000000000000000000000000000000000"
        "900463ffffffff16806360fe47b114604e5780636d4ce63c146078575b600080fd5b348015605957600080fd5b"
        "5060766004803603810190808035906020019092919050505060a0565b005b348015608357600080fd5b50608a"
        "60aa565b6040518082815260200191505060405180910390f35b8060008190555050565b600080549050905600"
        "a165627a7a723058206ed282a30254e86080aeca513abfda612e925676970bdf4f17a8e5d36bcd8d230029");
    data = fromHex("0x6d4ce63c");
    isCreate = false;
    isStaticCall = true;

    result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);
    BOOST_CHECK(0 == result.status_code);

    // call function set(456)
    code = fromHex(
        "6080604052600436106049576000357c0100000000000000000000000000000000000000000000000000000000"
        "900463ffffffff16806360fe47b114604e5780636d4ce63c146078575b600080fd5b348015605957600080fd5b"
        "5060766004803603810190808035906020019092919050505060a0565b005b348015608357600080fd5b50608a"
        "60aa565b6040518082815260200191505060405180910390f35b8060008190555050565b600080549050905600"
        "a165627a7a723058206ed282a30254e86080aeca513abfda612e925676970bdf4f17a8e5d36bcd8d230029");
    data = fromHex("0x60fe47b100000000000000000000000000000000000000000000000000000000000001c8");
    isCreate = false;
    isStaticCall = false;

    result = evmc.execute(
        schedule, code, data, destination, caller, value, gas, depth, isCreate, isStaticCall);
    printResult(result);
    printAccount(destination);
    BOOST_CHECK(0 == result.status_code);

    u256 xResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(u256(456), xResult);
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
