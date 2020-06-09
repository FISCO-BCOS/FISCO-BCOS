/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libblockverifier/ExecutiveContext.h>
#include <libconfig/GlobalConfigure.h>
#include <libethcore/Exceptions.h>
#include <libexecutive/EVMHostContext.h>
#include <libexecutive/EVMHostInterface.h>
#include <libexecutive/EVMInstance.h>
#include <libexecutive/StateFace.h>
#include <libinterpreter/Instruction.h>
#include <libinterpreter/interpreter.h>
#include <libmptstate/MPTState.h>
#include <libstorage/MemoryTableFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::test;
using namespace dev::eth;
using namespace dev::mptstate;
using namespace dev::executive;


namespace
{
BlockHeader initBlockHeader()
{
    BlockHeader blockHeader;
    blockHeader.setGasLimit(0x7fffffffffffffff);
    blockHeader.setTimestamp(0);
    return blockHeader;
}

class Create2TestFixture : public TestOutputHelperFixture
{
public:
    explicit Create2TestFixture(EVMInterface* _vm)
      : mptState(std::make_shared<MPTState>(
            u256(0), MPTState::openDB("./", h256("0x2234")), BaseState::Empty)),
        state(mptState),
        vm{_vm}
    {
        state->addBalance(address, 1 * ether);
        blockverifier::ExecutiveContext::Ptr executiveContext =
            make_shared<blockverifier::ExecutiveContext>();
        executiveContext->setMemoryTableFactory(make_shared<storage::MemoryTableFactory>());
        envInfo.setPrecompiledEngine(executiveContext);
    }
    std::shared_ptr<eth::Result> callVMExec()
    {
        EVMHostContext extVm(state, envInfo, address, address, address, value, gasPrice,
            ref(inputData), code, crypto::Hash(code), depth, isCreate, staticCall);
        evmc_call_kind kind = extVm.isCreate() ? EVMC_CREATE : EVMC_CALL;
        uint32_t flags = extVm.staticCall() ? EVMC_STATIC : 0;
        auto leftGas = 20000000;
        evmc_message msg{toEvmC(extVm.myAddress()), toEvmC(extVm.caller()), toEvmC(extVm.value()),
            extVm.data().data(), extVm.data().size(), toEvmC(extVm.codeHash()),
            toEvmC(0x0_cppui256), leftGas, static_cast<int32_t>(extVm.depth()), kind, flags};
        evmc_revision revision = EVMC_CONSTANTINOPLE;
        auto ret = vm->exec(extVm, revision, &msg, extVm.code().data(), extVm.code().size());
        return ret;
    }
    void testCreate2worksInConstantinople()
    {
        callVMExec();
        BOOST_REQUIRE(state->addressHasCode(expectedAddress));
    }

    void testCreate2succeedsIfAddressHasEther()
    {
        state->addBalance(expectedAddress, 1 * ether);
        callVMExec();
        BOOST_REQUIRE(state->addressHasCode(expectedAddress));
    }

    void testCreate2doesntChangeContractIfAddressExists()
    {
        state->setCode(expectedAddress, bytes{inputData});
        callVMExec();
        BOOST_REQUIRE(state->code(expectedAddress) == inputData);
    }

    void testCreate2isForbiddenInStaticCall()
    {
        staticCall = true;
        auto result = callVMExec();
        BOOST_TEST(result->status() == EVMC_STATIC_MODE_VIOLATION);
    }

    BlockHeader blockHeader{initBlockHeader()};
    static dev::h256 fakeCallBack(int64_t) { return h256(); }
    EnvInfo envInfo{blockHeader, fakeCallBack, 0};
    Address address{KeyPair::create().address()};

    std::shared_ptr<MPTState> mptState;
    std::shared_ptr<StateFace> state;

    u256 value = 0;
    u256 gasPrice = 1;
    int depth = 0;
    bool isCreate = false;
    bool staticCall = false;
    u256 gas = 1000000;

    // mstore(0, 0x60)
    // return(0, 0x20)
    bytes inputData = fromHex("606060005260206000f3");

    // let s : = calldatasize()
    // calldatacopy(0, 0, s)
    // create2(0, 0, s, 0x123)
    // pop
    bytes code = fromHex("368060006000376101238160006000f55050");

    Address expectedAddress = right160(crypto::Hash(
        fromHex("ff") + address.asBytes() + toBigEndian(0x123_cppui256) + crypto::Hash(inputData)));

    std::unique_ptr<EVMInterface> vm;
};

class AlethInterpreterCreate2TestFixture : public Create2TestFixture
{
public:
    AlethInterpreterCreate2TestFixture()
      : Create2TestFixture{new EVMInstance{evmc_create_interpreter()}}
    {}
};

class ExtcodehashTestFixture : public TestOutputHelperFixture
{
public:
    explicit ExtcodehashTestFixture(EVMInterface* _vm)
      : mptState(std::make_shared<MPTState>(
            u256(0), MPTState::openDB("./", h256("0x2234")), BaseState::Empty)),
        state(mptState),
        vm{_vm}
    {
        state->addBalance(address, 1 * ether);
        state->setCode(extAddress, bytes{extCode});
        blockverifier::ExecutiveContext::Ptr executiveContext =
            make_shared<blockverifier::ExecutiveContext>();
        executiveContext->setMemoryTableFactory(make_shared<storage::MemoryTableFactory>());
        envInfo.setPrecompiledEngine(executiveContext);
    }
    owning_bytes_ref callVMExec(bytesConstRef const& _addressRef)
    {
        EVMHostContext extVm(state, envInfo, address, address, address, value, gasPrice,
            _addressRef, code, crypto::Hash(code), depth, isCreate, staticCall);
        evmc_call_kind kind = extVm.isCreate() ? EVMC_CREATE : EVMC_CALL;
        uint32_t flags = extVm.staticCall() ? EVMC_STATIC : 0;
        auto leftGas = 20000;
        evmc_message msg{toEvmC(extVm.myAddress()), toEvmC(extVm.caller()), toEvmC(extVm.value()),
            extVm.data().data(), extVm.data().size(), toEvmC(extVm.codeHash()),
            toEvmC(0x0_cppui256), leftGas, static_cast<int32_t>(extVm.depth()), kind, flags};
        evmc_revision revision = EVMC_CONSTANTINOPLE;
        auto ret = vm->exec(extVm, revision, &msg, extVm.code().data(), extVm.code().size());
        auto outputRef = ret->output();
        return owning_bytes_ref(
            bytes(outputRef.data(), outputRef.data() + outputRef.size()), 0, outputRef.size());
    }
    void testExtcodehashWorksInConstantinople()
    {
        auto ret = callVMExec(extAddress.ref());
        BOOST_REQUIRE(ret.toBytes() == crypto::Hash(extCode).asBytes());
    }

    void testExtcodehashHasCorrectCost()
    {
        bigint gasBefore;
        bigint gasAfter;
#if 0
        auto onOp = [&gasBefore, &gasAfter](uint64_t,  // steps
                        uint64_t,                      // PC
                        evmc_opcode _instr,
                        bigint,  // newMemSize
                        bigint,  // gasCost
                        bigint _gas, EVMInterface const*, EVMHostContext const*) {
            if (_instr == evmc_opcode::OP_EXTCODEHASH)
                gasBefore = _gas;
            else if (gasBefore != 0 && gasAfter == 0)
                gasAfter = _gas;
        };
#endif
        callVMExec(extAddress.ref());
        BOOST_REQUIRE_EQUAL(gasBefore - gasAfter, 400);
    }

    void testExtCodeHashOfNonContractAccount()
    {
        Address addressWithEmptyCode{KeyPair::create().address()};
        state->addBalance(addressWithEmptyCode, 1 * ether);
        auto ret = callVMExec(addressWithEmptyCode.ref());
        if (g_BCOSConfig.SMCrypto())
        {
            BOOST_REQUIRE_EQUAL(toHex(ret.toBytes()),
                "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b");
        }
        else
        {
            BOOST_REQUIRE_EQUAL(toHex(ret.toBytes()),
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
        }
    }

    void testExtCodeHashOfNonExistentAccount()
    {
        Address addressNonExisting{0x1234};

        auto ret = callVMExec(addressNonExisting.ref());

        BOOST_REQUIRE_EQUAL(fromBigEndian<int>(ret.toBytes()), 0);
    }

    void testExtCodeHashOfPrecomileZeroBalance()
    {
        Address addressPrecompile{0x1};

        auto ret = callVMExec(addressPrecompile.ref());

        BOOST_REQUIRE_EQUAL(fromBigEndian<int>(ret.toBytes()), 0);
    }

    void testExtCodeHashOfPrecomileNonZeroBalance()
    {
        Address addressPrecompile{0x1};
        state->addBalance(addressPrecompile, 1 * ether);

        auto ret = callVMExec(addressPrecompile.ref());

        if (g_BCOSConfig.SMCrypto())
        {
            BOOST_REQUIRE_EQUAL(toHex(ret.toBytes()),
                "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b");
        }
        else
        {
            BOOST_REQUIRE_EQUAL(toHex(ret.toBytes()),
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
        }
    }

    void testExtcodehashIgnoresHigh12Bytes()
    {
        // calldatacopy(0, 0, 32)
        // let addr : = mload(0)
        // let hash : = extcodehash(addr)
        // mstore(0, hash)
        // return(0, 32)
        code = fromHex("60206000600037600051803f8060005260206000f35050");

        bytes extAddressPrefixed =
            bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc} + extAddress.ref();

        EVMHostContext extVm(state, envInfo, address, address, address, value, gasPrice,
            ref(extAddressPrefixed), code, crypto::Hash(code), depth, isCreate, staticCall);

        auto ret = callVMExec(ref(extAddressPrefixed));

        BOOST_REQUIRE(ret.toBytes() == crypto::Hash(extCode).asBytes());
    }

    BlockHeader blockHeader{initBlockHeader()};
    static dev::h256 fakeCallBack(int64_t) { return h256(); }
    EnvInfo envInfo{blockHeader, fakeCallBack, 0};
    Address address{KeyPair::create().address()};
    Address extAddress{KeyPair::create().address()};

    std::shared_ptr<MPTState> mptState;
    std::shared_ptr<StateFace> state;

    u256 value = 0;
    u256 gasPrice = 1;
    int depth = 0;
    bool isCreate = false;
    bool staticCall = false;
    u256 gas = 1000000;

    // mstore(0, 0x60)
    // return(0, 0x20)
    bytes extCode = fromHex("606060005260206000f3");

    // calldatacopy(12, 0, 20)
    // let addr : = mload(0)
    // let hash : = extcodehash(addr)
    // mstore(0, hash)
    // return(0, 32)
    bytes code = fromHex("60146000600c37600051803f8060005260206000f35050");

    std::unique_ptr<EVMInterface> vm;
};  // namespace

class AlethInterpreterExtcodehashTestFixture : public ExtcodehashTestFixture
{
public:
    AlethInterpreterExtcodehashTestFixture()
      : ExtcodehashTestFixture{new EVMInstance{evmc_create_interpreter()}}
    {}
};

class SM_AlethInterpreterExtcodehashTestFixture : public SM_CryptoTestFixture,
                                                  public AlethInterpreterExtcodehashTestFixture
{
public:
    SM_AlethInterpreterExtcodehashTestFixture()
      : SM_CryptoTestFixture(), AlethInterpreterExtcodehashTestFixture()
    {}
};


}  // namespace


BOOST_FIXTURE_TEST_SUITE(AlethInterpreterSuite, TestOutputHelperFixture)
BOOST_FIXTURE_TEST_SUITE(AlethInterpreterCreate2Suite, AlethInterpreterCreate2TestFixture)

BOOST_AUTO_TEST_CASE(AlethInterpreterCreate2worksInConstantinople)
{
    testCreate2worksInConstantinople();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterCreate2succeedsIfAddressHasEther)
{
    testCreate2succeedsIfAddressHasEther();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterCreate2doesntChangeContractIfAddressExists)
{
    testCreate2doesntChangeContractIfAddressExists();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterCreate2isForbiddenInStaticCall)
{
    testCreate2isForbiddenInStaticCall();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(AlethInterpreterExtcodehashSuite, AlethInterpreterExtcodehashTestFixture)

BOOST_AUTO_TEST_CASE(AlethInterpreterExtcodehashWorksInConstantinople)
{
    testExtcodehashWorksInConstantinople();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterExtCodeHashOfNonContractAccount)
{
    testExtCodeHashOfNonContractAccount();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterExtCodeHashOfNonExistentAccount)
{
    testExtCodeHashOfPrecomileZeroBalance();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterExtCodeHashOfPrecomileZeroBalance)
{
    testExtCodeHashOfNonExistentAccount();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterExtCodeHashOfPrecomileNonZeroBalance)
{
    testExtCodeHashOfPrecomileNonZeroBalance();
}

BOOST_AUTO_TEST_CASE(AlethInterpreterExtCodeHashIgnoresHigh12Bytes)
{
    testExtcodehashIgnoresHigh12Bytes();
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(
    SM_AlethInterpreterExtcodehashSuite, SM_AlethInterpreterExtcodehashTestFixture)

BOOST_AUTO_TEST_CASE(SM_AlethInterpreterExtCodeHashOfNonContractAccount)
{
    testExtCodeHashOfNonContractAccount();
}

BOOST_AUTO_TEST_CASE(SM_AlethInterpreterExtCodeHashOfPrecomileNonZeroBalance)
{
    testExtCodeHashOfPrecomileNonZeroBalance();
}

BOOST_AUTO_TEST_SUITE_END()
