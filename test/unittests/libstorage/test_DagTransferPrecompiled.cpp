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
/** @file test_DagTransferPrecompiled.cpp
 *  @author octopuswang
 *  @date 20190111
 */
#include "Common.h"
#include "MemoryStorage.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/extension/DagTransferPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_DagTransferPrecompiled
{
struct DagTransferPrecompiledFixture
{
    DagTransferPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        ExecutiveContextFactory factory(precompiledExecResultFactory);

        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
        factory.setTableFactoryFactory(tableFactoryFactory);
        context = factory.createExecutiveContext(blockInfo, h256(0));
        dtPrecompiled = std::make_shared<DagTransferPrecompiled>();
        dtPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
        memoryTableFactory = context->getMemoryTableFactory();
    }

    ~DagTransferPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    DagTransferPrecompiled::Ptr dtPrecompiled;
    BlockInfo blockInfo;

    std::string userAddFunc{"userAdd(string,uint256)"};
    std::string userSaveFunc{"userSave(string,uint256)"};
    std::string userDrawFunc{"userDraw(string,uint256)"};
    std::string userTransferFunc{"userTransfer(string,string,uint256)"};
    std::string userBalanceFunc{"userBalance(string)"};
};

BOOST_FIXTURE_TEST_SUITE(test_DagTransferPrecompiled, DagTransferPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(dtPrecompiled->toString() == "DagTransfer");
}

BOOST_AUTO_TEST_CASE(isParallelPrecompiled)
{
    auto ret = dtPrecompiled->isParallelPrecompiled();
    BOOST_TEST(ret);
}

BOOST_AUTO_TEST_CASE(invalidUserName)
{
    std::string user0;
    BOOST_REQUIRE(user0.empty());
    bool ret = dtPrecompiled->invalidUserName(user0);
    BOOST_TEST(ret);

    std::string user1 = "user";
    BOOST_REQUIRE(!user1.empty());
    ret = dtPrecompiled->invalidUserName(user1);
    BOOST_TEST((!ret));
}

BOOST_AUTO_TEST_CASE(getParallelTag)
{
    // valid user name with valid amount
    std::string user = "user";
    dev::u256 amount = 1111111;
    dev::eth::ContractABI abi;
    bytes param;

    std::vector<std::string> vTags;
    // add
    param = abi.abiIn(userAddFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(((vTags.size() == 1) && (vTags[0] == user)));
    // save
    param = abi.abiIn(userSaveFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(((vTags.size() == 1) && (vTags[0] == user)));
    // draw
    param = abi.abiIn(userDrawFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(((vTags.size() == 1) && (vTags[0] == user)));
    // balance
    param = abi.abiIn(userBalanceFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());

    // invalid user name
    user = "";
    amount = 0;
    // add
    param = abi.abiIn(userAddFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());
    // save
    param = abi.abiIn(userSaveFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());
    // draw
    param = abi.abiIn(userDrawFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());
    // balance
    param = abi.abiIn(userBalanceFunc, user, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());

    // transfer test
    // valid input parameters
    std::string from = "from";
    std::string to = "to";
    amount = 1111111;
    param = abi.abiIn(userTransferFunc, from, to, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(((vTags.size() == 2) && (vTags[0] == from) && (vTags[1] == to)));

    // from user empty
    from = "";
    to = "to";
    amount = 1111111;
    param = abi.abiIn(userTransferFunc, from, to, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());

    // to user empty
    from = "from";
    to = "";
    amount = 1111111;
    param = abi.abiIn(userTransferFunc, from, to, amount);
    vTags = dtPrecompiled->getParallelTag(bytesConstRef(&param));
    BOOST_TEST(vTags.empty());
}

BOOST_AUTO_TEST_CASE(userAdd)
{  // function userAdd(string user, uint256 balance) public returns(bool);
    Address origin;
    dev::eth::ContractABI abi;

    std::string user;
    dev::u256 amount;
    bytes out;
    dev::u256 result;
    bytes params;

    // invalid input, user name empty string
    user = "";
    params = abi.abiIn(userAddFunc, user, amount);
    auto callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();

    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);

    // normal input, first add this user
    user = "user";
    amount = 11111;
    params = abi.abiIn(userAddFunc, user, amount);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // user already exist, add this user again
    user = "user";
    amount = 11111;
    params = abi.abiIn(userAddFunc, user, amount);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_ALREADY_EXIST == result);
}

BOOST_AUTO_TEST_CASE(userSave)
{  // function userSave(string user, uint256 balance) public returns(bool);
    Address origin;
    dev::eth::ContractABI abi;

    std::string user;
    dev::u256 amount;
    bytes out;
    dev::u256 result;
    bytes params;

    // invalid input, user name empty string
    user = "";
    params = abi.abiIn(userSaveFunc, user, amount);
    auto callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);


    // invalid input, amount zero
    user = "user";
    dev::u256 amount0 = 0;
    params = abi.abiIn(userSaveFunc, user, amount0);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_AMOUNT == result);

    // normal input, user is not exist, add this user.
    user = "user";
    dev::u256 amount1 = 1111;
    params = abi.abiIn(userSaveFunc, user, amount1);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // normal input, user exist, add balance of this user.
    user = "user";
    dev::u256 amount2 = 1111;
    params = abi.abiIn(userSaveFunc, user, amount2);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // get balance of this user
    dev::u256 balance;
    params = abi.abiIn(userBalanceFunc, user);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(((0 == result) && (balance == (amount1 + amount2))));

    // normal input, user exist, add balance of this user, balance overflow.
    user = "user";
    dev::u256 amount3 =
        dev::u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params = abi.abiIn(userSaveFunc, user, amount3);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_BALANCE_OVERFLOW == result);
}

BOOST_AUTO_TEST_CASE(userDraw)
{  // function userDraw(string user, uint256 balance) public returns(bool);
    Address origin;
    dev::eth::ContractABI abi;

    std::string user;
    bytes out;
    dev::u256 result;
    bytes params;

    // invalid input, user name empty string
    user = "";
    dev::u256 amount0 = 0;
    params = abi.abiIn(userDrawFunc, user, amount0);
    auto callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);


    // invalid input, amount zero
    user = "user";
    dev::u256 amount1 = 0;
    params = abi.abiIn(userDrawFunc, user, amount1);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_AMOUNT == result);

    // add user first, after this the balance of "user" is 11111
    user = "user";
    dev::u256 amount2 = 11111;
    params = abi.abiIn(userAddFunc, user, amount2);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // draw 11110 , after this the balance of "user" is 1
    user = "user";
    dev::u256 amount3 = 11110;
    params = abi.abiIn(userDrawFunc, user, amount3);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // draw 11110  again, insufficient balance
    dev::u256 amount4 = 11110;
    params = abi.abiIn(userDrawFunc, user, amount4);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_INSUFFICIENT_BALANCE == result);

    // get balance of this user
    dev::u256 balance;
    params = abi.abiIn(userBalanceFunc, user);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(((result == 0) && (balance == (amount2 - amount3))));
}

BOOST_AUTO_TEST_CASE(userBalance)
{  // function userBalance(string user) public constant returns(bool,uint256);
    Address origin;
    dev::eth::ContractABI abi;

    std::string user;
    dev::u256 balance;
    dev::u256 result;
    bytes out;
    bytes params;
    dev::u256 amount;

    // invalid input, user name empty string
    user = "";
    balance = 0;
    params = abi.abiIn(userBalanceFunc, user);
    auto callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);

    // normal input, user not exist
    user = "user";
    balance = 0;
    params = abi.abiIn(userBalanceFunc, user);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(CODE_INVALID_USER_NOT_EXIST == result);

    // create user
    user = "user";
    amount = 1111111;
    params = abi.abiIn(userAddFunc, user, amount);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(0 == result);

    // get balance of user
    params = abi.abiIn(userBalanceFunc, user);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(((result == 0) && (balance == amount)));
}

BOOST_AUTO_TEST_CASE(userTransfer)
{  // function userTransfer(string user_a, string user_b, uint256 amount) public returns(bool);
    Address origin;
    dev::eth::ContractABI abi;

    std::string from, to;
    // dev::u256 amount;
    dev::s256 result;
    bytes out;
    bytes params;

    dev::u256 amount0;
    // invalid input, from user name empty string, to user name empty string.
    params = abi.abiIn(userTransferFunc, from, to, amount0);
    auto callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);

    // invalid input, from user name empty string.
    from = "";
    to = "to";
    dev::u256 amount1 = 12345;
    params = abi.abiIn(userTransferFunc, from, to, amount1);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);

    // invalid input, to user name empty string.
    from = "from";
    to = "";
    dev::u256 amount2 = 12345;
    params = abi.abiIn(userTransferFunc, from, to, amount2);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NAME == result);

    // invalid input, amount zero.
    from = "from";
    to = "to";
    dev::u256 amount3 = 0;
    params = abi.abiIn(userTransferFunc, from, to, amount3);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_AMOUNT == result);

    // from and to user all not exist
    from = "from";
    to = "to";
    dev::u256 amount4 = 11111;
    params = abi.abiIn(userTransferFunc, from, to, amount4);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_USER_NOT_EXIST == result);

    // insert three user: user0(111111)  user1(2222222)
    // user3(0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff)
    std::string user0 = "user0";
    dev::u256 amount5 = 111111;
    params = abi.abiIn(userAddFunc, user0, amount5);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    std::string user1 = "user1";
    dev::u256 amount6 = 2222222;
    params = abi.abiIn(userAddFunc, user1, amount6);
    out = callResult->execResult();
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    std::string user2 = "user2";
    dev::u256 amount7("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params = abi.abiIn(userAddFunc, user2, amount7);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // user0 transfer 111110 to user1
    dev::u256 transfer = 111110;
    params = abi.abiIn(userTransferFunc, user0, user1, transfer);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(0 == result);

    // user0 transfer 111110 to user1 again
    transfer = 111110;
    params = abi.abiIn(userTransferFunc, user0, user1, transfer);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_INSUFFICIENT_BALANCE == result);

    dev::u256 balance;
    // get balance of user0
    params = abi.abiIn(userBalanceFunc, user0);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(((result == 0) && (balance == (amount5 - transfer))));

    params = abi.abiIn(userBalanceFunc, user1);
    // get balance of user1
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result, balance);
    BOOST_TEST(((result == 0) && (balance == (amount6 + transfer))));

    // user1 transfer 111110 to user2, balance of user2 will overflow
    params = abi.abiIn(userTransferFunc, user1, user2, transfer);
    callResult = dtPrecompiled->call(context, bytesConstRef(&params), origin);
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), result);
    BOOST_TEST(CODE_INVALID_BALANCE_OVERFLOW == result);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_DagTransferPrecompiled
