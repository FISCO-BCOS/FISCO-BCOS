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
 * @file: TransactionException.cpp
 * @author: xingqiangbai
 * @date: 20200608
 */

#include "TransactionException.h"
#include "Exceptions.h"

using namespace std;
using namespace bcos;
using namespace bcos::protocol;

TransactionException bcos::protocol::toTransactionException(Exception const& _e)
{
    // Basic Transaction exceptions
    if (!!dynamic_cast<RLPException const*>(&_e))
        return TransactionException::BadRLP;
    if (!!dynamic_cast<OutOfGasIntrinsic const*>(&_e))
        return TransactionException::OutOfGasIntrinsic;
    if (!!dynamic_cast<InvalidSignature const*>(&_e))
        return TransactionException::InvalidSignature;
    if (!!dynamic_cast<PermissionDenied const*>(&_e))
        return TransactionException::PermissionDenied;

    // Executive exceptions
    if (!!dynamic_cast<OutOfGasBase const*>(&_e))
        return TransactionException::OutOfGasBase;
    if (!!dynamic_cast<InvalidNonce const*>(&_e))
        return TransactionException::InvalidNonce;
    if (!!dynamic_cast<NotEnoughCash const*>(&_e))
        return TransactionException::NotEnoughCash;
    if (!!dynamic_cast<BlockGasLimitReached const*>(&_e))
        return TransactionException::BlockGasLimitReached;
    if (!!dynamic_cast<AddressAlreadyUsed const*>(&_e))
        return TransactionException::AddressAlreadyUsed;
    if (!!dynamic_cast<PrecompiledError const*>(&_e))
        return TransactionException::PrecompiledError;

    // VM execution exceptions
    if (!!dynamic_cast<BadInstruction const*>(&_e))
        return TransactionException::BadInstruction;
    if (!!dynamic_cast<BadJumpDestination const*>(&_e))
        return TransactionException::BadJumpDestination;
    if (!!dynamic_cast<OutOfGas const*>(&_e))
        return TransactionException::OutOfGas;
    if (!!dynamic_cast<OutOfStack const*>(&_e))
        return TransactionException::OutOfStack;
    if (!!dynamic_cast<StackUnderflow const*>(&_e))
        return TransactionException::StackUnderflow;

    return TransactionException::Unknown;
}

std::ostream& bcos::protocol::operator<<(std::ostream& _out, TransactionException const& _er)
{
    switch (_er)
    {
    case TransactionException::None:
        _out << "None";
        break;
    case TransactionException::BadRLP:
        _out << "BadRLP";
        break;
    case TransactionException::InvalidFormat:
        _out << "InvalidFormat";
        break;
    case TransactionException::OutOfGasIntrinsic:
        _out << "OutOfGasIntrinsic";
        break;
    case TransactionException::InvalidSignature:
        _out << "InvalidSignature";
        break;
    case TransactionException::InvalidNonce:
        _out << "InvalidNonce";
        break;
    case TransactionException::NotEnoughCash:
        _out << "NotEnoughCash";
        break;
    case TransactionException::OutOfGasBase:
        _out << "OutOfGasBase";
        break;
    case TransactionException::BlockGasLimitReached:
        _out << "BlockGasLimitReached";
        break;
    case TransactionException::BadInstruction:
        _out << "BadInstruction";
        break;
    case TransactionException::BadJumpDestination:
        _out << "BadJumpDestination";
        break;
    case TransactionException::OutOfGas:
        _out << "OutOfGas";
        break;
    case TransactionException::OutOfStack:
        _out << "OutOfStack";
        break;
    case TransactionException::StackUnderflow:
        _out << "StackUnderflow";
        break;
    case TransactionException::NonceCheckFail:
        _out << "NonceCheckFail";
        break;
    case TransactionException::BlockLimitCheckFail:
        _out << "BlockLimitCheckFail";
        break;
    case TransactionException::FilterCheckFail:
        _out << "FilterCheckFail";
        break;
    case TransactionException::NoDeployPermission:
        _out << "NoDeployPermission";
        break;
    case TransactionException::NoCallPermission:
        _out << "NoCallPermission";
        break;
    case TransactionException::NoTxPermission:
        _out << "NoTxPermission";
        break;
    case TransactionException::PrecompiledError:
        _out << "PrecompiledError";
        break;
    case TransactionException::RevertInstruction:
        _out << "RevertInstruction";
        break;
    case TransactionException::InvalidZeroSignatureFormat:
        _out << "InvalidZeroSignatureFormat";
        break;
    case TransactionException::AddressAlreadyUsed:
        _out << "AddressAlreadyUsed";
        break;
    case TransactionException::PermissionDenied:
        _out << "PermissionDenied";
        break;
    case TransactionException::CallAddressError:
        _out << "CallAddressError";
        break;
    case TransactionException::GasOverflow:
        _out << "GasOverflow";
        break;
    case TransactionException::TxPoolIsFull:
        _out << "TxPoolIsFull";
        break;
    case TransactionException::TransactionRefused:
        _out << "TransactionRefused";
        break;
    case TransactionException::ContractFrozen:
        _out << "ContractFrozen";
        break;
    case TransactionException::AccountFrozen:
        _out << "AccountFrozen";
        break;
    case TransactionException::AlreadyKnown:
        _out << "StackUnderflow";
        break;
    case TransactionException::AlreadyInChain:
        _out << "AlreadyInChain";
        break;
    case TransactionException::InvalidChainId:
        _out << "InvalidChainId";
        break;
    case TransactionException::InvalidGroupId:
        _out << "InvalidGroupId";
        break;
    case TransactionException::RequestNotBelongToTheGroup:
        _out << "RequestNotBelongToTheGroup";
        break;
    case TransactionException::MalformedTx:
        _out << "MalformedTx";
        break;
    case TransactionException::OverGroupMemoryLimit:
        _out << "OverGroupMemoryLimit";
        break;
    default:
        _out << "Unknown";
        break;
    }
    return _out;
}
