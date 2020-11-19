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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @file: TransactionException.h
 * @author: xingqiangbai
 * @date: 20200608
 */


#pragma once

#include <iostream>
#include <sstream>

namespace bcos
{
struct Exception;
namespace eth
{
enum class TransactionException : uint32_t
{
    None = 0,
    Unknown = 1,
    BadRLP = 2,
    InvalidFormat = 3,
    OutOfGasIntrinsic = 4,  ///< Too little gas to pay for the base transaction cost.
    InvalidSignature = 5,
    InvalidNonce = 6,
    NotEnoughCash = 7,
    OutOfGasBase = 8,  ///< Too little gas to pay for the base transaction cost.
    BlockGasLimitReached = 9,
    BadInstruction = 10,
    BadJumpDestination = 11,
    OutOfGas = 12,    ///< Ran out of gas executing code of the transaction.
    OutOfStack = 13,  ///< Ran out of stack executing code of the transaction.
    StackUnderflow = 14,
    NonceCheckFail = 15,  // noncecheck ok() ==  false
    BlockLimitCheckFail = 16,
    FilterCheckFail = 17,
    NoDeployPermission = 18,
    NoCallPermission = 19,
    NoTxPermission = 20,
    PrecompiledError = 21,
    RevertInstruction = 22,
    InvalidZeroSignatureFormat = 23,
    AddressAlreadyUsed = 24,
    PermissionDenied = 25,
    CallAddressError = 26,
    GasOverflow = 27,
    TxPoolIsFull = 28,
    TransactionRefused = 29,
    ContractFrozen = 30,
    AccountFrozen = 31,
    AlreadyKnown = 10000,  /// txPool related errors
    AlreadyInChain = 10001,
    InvalidChainId = 10002,
    InvalidGroupId = 10003,
    RequestNotBelongToTheGroup = 10004,
    MalformedTx = 10005,
    OverGroupMemoryLimit = 10006
};

TransactionException toTransactionException(Exception const& _e);
std::ostream& operator<<(std::ostream& _out, TransactionException const& _er);

inline std::string toJS(eth::TransactionException const& _i)
{
    std::stringstream stream;
    stream << "0x" << std::hex << static_cast<int>(_i);
    return stream.str();
}
}  // namespace eth
}  // namespace bcos
