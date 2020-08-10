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
 * @file ExecutionResultTest.cpp
 * @author: jimmyshi
 * @date 2018-09-21
 */

#include <libdevcore/CommonJS.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <libexecutive/EVMHostContext.h>
#include <libexecutive/Executive.h>
#include <libmptstate/MPTState.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>


using namespace std;
using namespace dev::eth;
using namespace dev::executive;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ExecutionResultTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(ExecutionResultOutput)
{
#if 0
    std::stringstream buffer;
    ExecutionResult exRes;

    exRes.gasUsed = u256("12345");
    exRes.newAddress = Address("a94f5374fce5edbc8e2a8697c15331677e6ebf0b");
    exRes.output = fromHex("001122334455");

    buffer << exRes;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "{12345, a94f5374fce5edbc8e2a8697c15331677e6ebf0b, 001122334455}",
        "Error ExecutionResultOutput");
#endif
}

BOOST_AUTO_TEST_CASE(transactionExceptionOutput)
{
    std::stringstream buffer;
    buffer << TransactionException::BadInstruction;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "BadInstruction", "Error output TransactionException::BadInstruction");
    buffer.str(std::string());

    buffer << TransactionException::None;
    BOOST_CHECK_MESSAGE(buffer.str() == "None", "Error output TransactionException::None");
    buffer.str(std::string());

    buffer << TransactionException::BadRLP;
    BOOST_CHECK_MESSAGE(buffer.str() == "BadRLP", "Error output TransactionException::BadRLP");
    buffer.str(std::string());

    buffer << TransactionException::InvalidFormat;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "InvalidFormat", "Error output TransactionException::InvalidFormat");
    buffer.str(std::string());

    buffer << TransactionException::OutOfGasIntrinsic;
    BOOST_CHECK_MESSAGE(buffer.str() == "OutOfGasIntrinsic",
        "Error output TransactionException::OutOfGasIntrinsic");
    buffer.str(std::string());

    buffer << TransactionException::InvalidSignature;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "InvalidSignature", "Error output TransactionException::InvalidSignature");
    buffer.str(std::string());

    buffer << TransactionException::InvalidNonce;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "InvalidNonce", "Error output TransactionException::InvalidNonce");
    buffer.str(std::string());

    buffer << TransactionException::NotEnoughCash;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "NotEnoughCash", "Error output TransactionException::NotEnoughCash");
    buffer.str(std::string());

    buffer << TransactionException::OutOfGasBase;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "OutOfGasBase", "Error output TransactionException::OutOfGasBase");
    buffer.str(std::string());

    buffer << TransactionException::BlockGasLimitReached;
    BOOST_CHECK_MESSAGE(buffer.str() == "BlockGasLimitReached",
        "Error output TransactionException::BlockGasLimitReached");
    buffer.str(std::string());

    buffer << TransactionException::BadInstruction;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "BadInstruction", "Error output TransactionException::BadInstruction");
    buffer.str(std::string());

    buffer << TransactionException::BadJumpDestination;
    BOOST_CHECK_MESSAGE(buffer.str() == "BadJumpDestination",
        "Error output TransactionException::BadJumpDestination");
    buffer.str(std::string());

    buffer << TransactionException::OutOfGas;
    BOOST_CHECK_MESSAGE(buffer.str() == "OutOfGas", "Error output TransactionException::OutOfGas");
    buffer.str(std::string());

    buffer << TransactionException::OutOfStack;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "OutOfStack", "Error output TransactionException::OutOfStack");
    buffer.str(std::string());

    buffer << TransactionException::StackUnderflow;
    BOOST_CHECK_MESSAGE(
        buffer.str() == "StackUnderflow", "Error output TransactionException::StackUnderflow");
    buffer.str(std::string());

    buffer << TransactionException(-1);
    BOOST_CHECK_MESSAGE(
        buffer.str() == "Unknown", "Error output TransactionException::StackUnderflow");
    buffer.str(std::string());
}

BOOST_AUTO_TEST_CASE(toTransactionExceptionConvert)
{
    RLPException rlpEx;  // toTransactionException(*(dynamic_cast<Exception*>
    BOOST_CHECK_MESSAGE(toTransactionException(rlpEx) == TransactionException::BadRLP,
        "RLPException !=> TransactionException");
    OutOfGasIntrinsic oogEx;
    BOOST_CHECK_MESSAGE(toTransactionException(oogEx) == TransactionException::OutOfGasIntrinsic,
        "OutOfGasIntrinsic !=> TransactionException");
    InvalidSignature sigEx;
    BOOST_CHECK_MESSAGE(toTransactionException(sigEx) == TransactionException::InvalidSignature,
        "InvalidSignature !=> TransactionException");
    OutOfGasBase oogbEx;
    BOOST_CHECK_MESSAGE(toTransactionException(oogbEx) == TransactionException::OutOfGasBase,
        "OutOfGasBase !=> TransactionException");
    InvalidNonce nonceEx;
    BOOST_CHECK_MESSAGE(toTransactionException(nonceEx) == TransactionException::InvalidNonce,
        "InvalidNonce !=> TransactionException");
    NotEnoughCash cashEx;
    BOOST_CHECK_MESSAGE(toTransactionException(cashEx) == TransactionException::NotEnoughCash,
        "NotEnoughCash !=> TransactionException");
    BlockGasLimitReached blGasEx;
    BOOST_CHECK_MESSAGE(
        toTransactionException(blGasEx) == TransactionException::BlockGasLimitReached,
        "BlockGasLimitReached !=> TransactionException");
    BadInstruction badInsEx;
    BOOST_CHECK_MESSAGE(toTransactionException(badInsEx) == TransactionException::BadInstruction,
        "BadInstruction !=> TransactionException");
    BadJumpDestination badJumpEx;
    BOOST_CHECK_MESSAGE(
        toTransactionException(badJumpEx) == TransactionException::BadJumpDestination,
        "BadJumpDestination !=> TransactionException");
    OutOfGas oogEx2;
    BOOST_CHECK_MESSAGE(toTransactionException(oogEx2) == TransactionException::OutOfGas,
        "OutOfGas !=> TransactionException");
    OutOfStack oosEx;
    BOOST_CHECK_MESSAGE(toTransactionException(oosEx) == TransactionException::OutOfStack,
        "OutOfStack !=> TransactionException");
    StackUnderflow stackEx;
    BOOST_CHECK_MESSAGE(toTransactionException(stackEx) == TransactionException::StackUnderflow,
        "StackUnderflow !=> TransactionException");
    Exception notEx;
    BOOST_CHECK_MESSAGE(toTransactionException(notEx) == TransactionException::Unknown,
        "Unexpected should be TransactionException::Unknown");
}

BOOST_AUTO_TEST_CASE(GettingSenderForUnsignedTransactionThrows)
{
    Transaction tx(0, 0, 10000, Address("a94f5374fce5edbc8e2a8697c15331677e6ebf0b"), bytes(), 0);
    BOOST_CHECK(!tx.hasSignature());

    BOOST_REQUIRE_THROW(tx.sender(), TransactionIsUnsigned);
}

BOOST_AUTO_TEST_CASE(GettingSignatureForUnsignedTransactionThrows)
{
    Transaction tx(0, 0, 10000, Address("a94f5374fce5edbc8e2a8697c15331677e6ebf0b"), bytes(), 0);
    BOOST_REQUIRE_THROW(tx.signature(), TransactionIsUnsigned);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
