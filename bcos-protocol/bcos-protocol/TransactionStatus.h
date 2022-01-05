/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file: TransactionStatus.h
 * @author: xingqiangbai
 * @date: 20200608
 */

#pragma once
#include <iostream>
#include <sstream>

namespace bcos
{
struct Exception;
namespace protocol
{
// TODO: redefine the status value when everything ready
enum class TransactionStatus : int32_t
{
    None = 0,
    Unknown = 1,
    OutOfGasLimit = 2,  ///< Too little gas to pay for the base transaction cost.
    NotEnoughCash = 7,  // TODO: remove this?
    BadInstruction = 10,
    BadJumpDestination = 11,
    OutOfGas = 12,    ///< Ran out of gas executing code of the transaction.
    OutOfStack = 13,  ///< Ran out of stack executing code of the transaction.
    StackUnderflow = 14,
    PrecompiledError = 15,
    RevertInstruction = 16,
    ContractAddressAlreadyUsed = 17,
    PermissionDenied = 18,
    CallAddressError = 19,
    GasOverflow = 20,
    ContractFrozen = 21,
    AccountFrozen = 22,
    WASMValidationFailure = 32,
    WASMArgumentOutOfRange = 33,
    WASMUnreachableInstruction = 34,
    WASMTrap = 35,
    NonceCheckFail = 10000,  /// txPool related errors
    BlockLimitCheckFail = 10001,
    TxPoolIsFull = 10002,
    Malform = 10003,
    AlreadyInTxPool = 10004,
    TxAlreadyInChain = 10005,
    InvalidChainId = 10006,
    InvalidGroupId = 10007,
    InvalidSignature = 10008,
    RequestNotBelongToTheGroup = 10009,
};

inline std::string toString(protocol::TransactionStatus const& _i)
{
    std::stringstream stream;
    stream << "0x" << std::hex << static_cast<int>(_i);
    return stream.str();
}

inline std::ostream& operator<<(std::ostream& _out, bcos::protocol::TransactionStatus const& _er)
{
    switch (_er)
    {
    case bcos::protocol::TransactionStatus::None:
        _out << "None";
        break;
    case bcos::protocol::TransactionStatus::OutOfGasLimit:
        _out << "OutOfGasLimit";
        break;
    case bcos::protocol::TransactionStatus::NotEnoughCash:
        _out << "NotEnoughCash";
        break;
    case bcos::protocol::TransactionStatus::BadInstruction:
        _out << "BadInstruction";
        break;
    case bcos::protocol::TransactionStatus::BadJumpDestination:
        _out << "BadJumpDestination";
        break;
    case bcos::protocol::TransactionStatus::OutOfGas:
        _out << "OutOfGas";
        break;
    case bcos::protocol::TransactionStatus::OutOfStack:
        _out << "OutOfStack";
        break;
    case bcos::protocol::TransactionStatus::StackUnderflow:
        _out << "StackUnderflow";
        break;
    case bcos::protocol::TransactionStatus::NonceCheckFail:
        _out << "NonceCheckFail";
        break;
    case bcos::protocol::TransactionStatus::BlockLimitCheckFail:
        _out << "BlockLimitCheckFail";
        break;
    case bcos::protocol::TransactionStatus::PrecompiledError:
        _out << "PrecompiledError";
        break;
    case bcos::protocol::TransactionStatus::RevertInstruction:
        _out << "RevertInstruction";
        break;
    case bcos::protocol::TransactionStatus::ContractAddressAlreadyUsed:
        _out << "ContractAddressAlreadyUsed";
        break;
    case bcos::protocol::TransactionStatus::PermissionDenied:
        _out << "PermissionDenied";
        break;
    case bcos::protocol::TransactionStatus::CallAddressError:
        _out << "CallAddressError";
        break;
    case bcos::protocol::TransactionStatus::GasOverflow:
        _out << "GasOverflow";
        break;
    case bcos::protocol::TransactionStatus::ContractFrozen:
        _out << "ContractFrozen";
        break;
    case bcos::protocol::TransactionStatus::AccountFrozen:
        _out << "AccountFrozen";
        break;
    case bcos::protocol::TransactionStatus::TxPoolIsFull:
        _out << "TxPoolIsFull";
        break;
    case bcos::protocol::TransactionStatus::Malform:
        _out << "MalformTx";
        break;
    case bcos::protocol::TransactionStatus::AlreadyInTxPool:
        _out << "AlreadyInTxPool";
        break;
    case bcos::protocol::TransactionStatus::TxAlreadyInChain:
        _out << "TxAlreadyInChain";
        break;
    case bcos::protocol::TransactionStatus::InvalidChainId:
        _out << "InvalidChainId";
        break;
    case bcos::protocol::TransactionStatus::InvalidGroupId:
        _out << "InvalidGroupId";
        break;
    case bcos::protocol::TransactionStatus::InvalidSignature:
        _out << "InvalidSignature";
        break;
    case bcos::protocol::TransactionStatus::RequestNotBelongToTheGroup:
        _out << "RequestNotBelongToTheGroup";
        break;
    default:
        _out << "Unknown";
        break;
    }
    return _out;
}
}  // namespace protocol
}  // namespace bcos
