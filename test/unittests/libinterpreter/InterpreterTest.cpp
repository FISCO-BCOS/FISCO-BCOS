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

    u256 ra = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000000");
    u256 rb = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000001");

    BOOST_CHECK_EQUAL(u256(66), ra);
    BOOST_CHECK_EQUAL(u256(66), rb);
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
        function C(int256 _a) {
            x1 = _a + 1;
            x2 = _a - 100;
            x3 = _a * -100;
            x4 = _a / 3;
            x5 = _a % 10;
            x6 = _a << 3;
            x7 = _a >> 1;
        }
    }

    param: 66 in hex 0000000000000000000000000000000000000000000000000000000000000042
    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(
        string("60606040523415600b57fe5b60405160208060e8833981016040528080519060200190919050505b600"
               "18101600081905550606481036001819055507fffffffffffffffffffffffffffffffffffffffffffff"
               "ffffffffffffffffff9c8102600281905550600381811515606f57fe5b05600381905550600a8181151"
               "5608157fe5b076004819055506003819060020a026005819055506001819060020a9005600681905550"
               "5b505b60338060b56000396000f30060606040525bfe00a165627a7a723058203749202eaef6dedbda1"
               "f890b19d4cebb77e8c3099346e8538470b2f671b312570029") +
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

    BOOST_CHECK_EQUAL(s256(66 + 1), x1);
    BOOST_CHECK_EQUAL(s256(66 - 100), x2);
    BOOST_CHECK_EQUAL(s256(66 * (-100)), x3);
    BOOST_CHECK_EQUAL(s256(66 / 3), x4);
    BOOST_CHECK_EQUAL(s256(66 % 10), x5);
    BOOST_CHECK_EQUAL(s256(66 << 3), x6);
    BOOST_CHECK_EQUAL(s256(66 >> 1), x7);
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
        }
    }

    */
    dev::eth::EVMSchedule const& schedule = DefaultSchedule;
    bytes code = fromHex(string(
        "60606040525b60008054600160a060020a03328116600160a060020a0319928316179092556001805430841690"
        "831617905560028054338416908316179055600380544190931692909116919091179055436004554460055545"
        "600655426007555a600855346009553a600a55600b805463ffffffff19167c0100000000000000000000000000"
        "000000000000000000000000000000600080357fffffffff000000000000000000000000000000000000000000"
        "000000000000001691909104919091179091556100d390600c90366100da565b505b61017a565b828054600181"
        "600116156101000203166002900490600052602060002090601f016020900481019282601f1061011b57828001"
        "60ff19823516178555610148565b82800160010185558215610148579182015b82811115610148578235825591"
        "60200191906001019061012d565b5b50610155929150610159565b5090565b61017791905b8082111561015557"
        "6000815560010161015f565b5090565b90565b6033806101886000396000f30060606040525bfe00a165627a7a"
        "723058201fbb3dbeecfad7633b95ad1ce5c1f4baaff733d5a8b2a8bf9385873f9d8ca4cc0029"));
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
    u256 gasResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000008");
    u256 valueResult = getStateValueU256(
        destination, "0000000000000000000000000000000000000000000000000000000000000009");
    u256 gasPriceResult = getStateValueU256(
        destination, "000000000000000000000000000000000000000000000000000000000000000a");

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
    // BOOST_CHECK(gasPriceResult == );
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
