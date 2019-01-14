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
 * @file Exceptions.cpp of Ethcore
 * @author: yujiechen
 * @date 2018-08-24
 */
#include <iostream>

#include <libdevcore/Assertions.h>
#include <libdevcore/Exceptions.h>
#include <libethcore/Exceptions.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(EthcoreExceptionTest, TestOutputHelperFixture)

/**
 * @brief : throw exception, and then check the error information
 *
 * @tparam E : type of error_info
 * @tparam T : data type of error_info encapsulated
 * @param error_info : detailed error information
 */
template <typename E, typename T>
void checkErrorInfo(const T& error_info)
{
    try
    {
        boost::throw_exception(Exception() << E(error_info));
    }
    catch (const Exception& e)
    {
        BOOST_CHECK(*(boost::get_error_info<E>(e)) == error_info);
    }
}

/// test exceptions of libethcore
BOOST_AUTO_TEST_CASE(testEthcoreException)
{
    /// gas related exceptions
    BOOST_CHECK_THROW(assertThrow(false, OutOfGasBase, "OutOfGasBase Exception"), OutOfGasBase);
    BOOST_CHECK_THROW(
        assertThrow(false, OutOfGasIntrinsic, "OutOfGasIntrinsic"), OutOfGasIntrinsic);
    BOOST_CHECK_THROW(assertThrow(false, NotEnoughCash, "NotEnoughCash"), NotEnoughCash);
    BOOST_CHECK_THROW(assertThrow(false, GasPriceTooLow, "GasPriceTooLow"), GasPriceTooLow);
    BOOST_CHECK_THROW(
        assertThrow(false, BlockGasLimitReached, "BlockGasLimitReached"), BlockGasLimitReached);
    BOOST_CHECK_THROW(assertThrow(false, TooMuchGasUsed, "TooMuchGasUsed"), TooMuchGasUsed);
    BOOST_CHECK_THROW(assertThrow(false, InvalidGasUsed, "InvalidGasUsed"), InvalidGasUsed);
    BOOST_CHECK_THROW(assertThrow(false, InvalidGasLimit, "InvalidGasLimit"), InvalidGasLimit);
    /// DB related exceptions
    BOOST_CHECK_THROW(assertThrow(false, NotEnoughAvailableSpace, "NotEnoughAvailableSpace"),
        NotEnoughAvailableSpace);
    BOOST_CHECK_THROW(assertThrow(false, ExtraDataTooBig, "ExtraDataTooBig"), ExtraDataTooBig);
    BOOST_CHECK_THROW(
        assertThrow(false, ExtraDataIncorrect, "ExtraDataIncorrect"), ExtraDataIncorrect);
    BOOST_CHECK_THROW(
        assertThrow(false, DatabaseAlreadyOpen, "DatabaseAlreadyOpen"), DatabaseAlreadyOpen);
    BOOST_CHECK_THROW(assertThrow(false, InvalidTransactionFormat, "InvalidTransactionFormat"),
        InvalidTransactionFormat);
    /// transaction related exceptions
    BOOST_CHECK_THROW(
        assertThrow(false, TransactionIsUnsigned, "TransactionIsUnsigned"), TransactionIsUnsigned);
    BOOST_CHECK_THROW(
        assertThrow(false, TransactionRefused, "TransactionRefused"), TransactionRefused);
    BOOST_CHECK_THROW(assertThrow(false, TransactionAlreadyInChain, "TransactionAlreadyInChain"),
        TransactionAlreadyInChain);
    /// state trie realted exceptions
    BOOST_CHECK_THROW(assertThrow(false, InvalidTransactionsRoot, "InvalidTransactionsRoot"),
        InvalidTransactionsRoot);
    BOOST_CHECK_THROW(assertThrow(false, InvalidReceiptsStateRoot, "InvalidReceiptsStateRoot"),
        InvalidReceiptsStateRoot);
    BOOST_CHECK_THROW(assertThrow(false, InvalidReceiptsStateRoot, "InvalidReceiptsStateRoot"),
        InvalidReceiptsStateRoot);
    /// block && block header related exceptions
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidParentHash, "InvalidParentHash"), InvalidParentHash);
    BOOST_CHECK_THROW(assertThrow(false, InvalidNumber, "InvalidNumber"), InvalidNumber);
    BOOST_CHECK_THROW(assertThrow(false, UnknownParent, "UnknownParent"), UnknownParent);
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidBlockFormat, "InvalidBlockFormat"), InvalidBlockFormat);
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidBlockHeaderItemCount, "InvalidBlockHeaderItemCount"),
        InvalidBlockHeaderItemCount);
    /// common exceptions
    BOOST_CHECK_THROW(assertThrow(false, InvalidNonce, "invalidNonce"), InvalidNonce);
    BOOST_CHECK_THROW(assertThrow(false, InvalidSignature, "InvalidSignature"), InvalidSignature);
    BOOST_CHECK_THROW(assertThrow(false, InvalidAddress, "InvalidAddress"), InvalidAddress);
    BOOST_CHECK_THROW(
        assertThrow(false, AddressAlreadyUsed, "AddressAlreadyUsed"), AddressAlreadyUsed);
    BOOST_CHECK_THROW(assertThrow(false, InvalidTimestamp, "InvalidTimestamp"), InvalidTimestamp);
    /// EVM related exceptions
    BOOST_CHECK_THROW(assertThrow(false, VMException, "VMException"), VMException);
    BOOST_CHECK_THROW(assertThrow(false, InternalVMError, "InternalVMError"), InternalVMError);
    BOOST_CHECK_THROW(assertThrow(false, BadInstruction, "BadInstruction"), BadInstruction);
    BOOST_CHECK_THROW(
        assertThrow(false, BadJumpDestination, "BadJumpDestination"), BadJumpDestination);
    BOOST_CHECK_THROW(assertThrow(false, OutOfGas, "OutOfGas"), OutOfGas);
    BOOST_CHECK_THROW(assertThrow(false, OutOfStack, "OutOfStack"), OutOfStack);
    BOOST_CHECK_THROW(assertThrow(false, StackUnderflow, "StackUnderflow"), StackUnderflow);
    BOOST_CHECK_THROW(
        assertThrow(false, DisallowedStateChange, "DisallowedStateChange"), DisallowedStateChange);
    BOOST_CHECK_THROW(assertThrow(false, BufferOverrun, "BufferOverrun"), BufferOverrun);
}

/// test error informations of libethcore
BOOST_AUTO_TEST_CASE(testEthcoreErrorInfo)
{
    checkErrorInfo<errinfo_name, std::string>("invalid name");
    checkErrorInfo<errinfo_field, int>(10);
    checkErrorInfo<errinfo_data, std::string>("error data");
    checkErrorInfo<errinfo_nonce, h64>(h64(10));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
