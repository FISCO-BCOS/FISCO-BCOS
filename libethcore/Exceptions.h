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
/** @file Exceptions.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * @author wheatli
 * @date 2018.8.27
 * @modify add VMException
 */

#pragma once

#include "Common.h"
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>

namespace dev
{
namespace eth
{
// information to add to exceptions
using errinfo_name = boost::error_info<struct tag_field, std::string>;
using errinfo_field = boost::error_info<struct tag_field, int>;
using errinfo_data = boost::error_info<struct tag_data, std::string>;
using errinfo_nonce = boost::error_info<struct tag_nonce, h64>;
using errinfo_target = boost::error_info<struct tag_target, h256>;
using errinfo_importResult = boost::error_info<struct tag_importResult, ImportResult>;
using BadFieldError = boost::tuple<errinfo_field, errinfo_data>;

/// gas related exceptions
DEV_SIMPLE_EXCEPTION(OutOfGasBase);
DEV_SIMPLE_EXCEPTION(OutOfGasIntrinsic);
DEV_SIMPLE_EXCEPTION(NotEnoughCash);
DEV_SIMPLE_EXCEPTION(GasPriceTooLow);
DEV_SIMPLE_EXCEPTION(BlockGasLimitReached);
DEV_SIMPLE_EXCEPTION(TooMuchGasUsed);
DEV_SIMPLE_EXCEPTION(InvalidGasUsed);
DEV_SIMPLE_EXCEPTION(InvalidGasLimit);

/// DB related exceptions
DEV_SIMPLE_EXCEPTION(NotEnoughAvailableSpace);
DEV_SIMPLE_EXCEPTION(ExtraDataTooBig);
DEV_SIMPLE_EXCEPTION(ExtraDataIncorrect);
DEV_SIMPLE_EXCEPTION(DatabaseAlreadyOpen);
DEV_SIMPLE_EXCEPTION(PermissionDenied);

/// transaction releated exceptions
DEV_SIMPLE_EXCEPTION(InvalidTransactionFormat);
DEV_SIMPLE_EXCEPTION(TransactionIsUnsigned);
DEV_SIMPLE_EXCEPTION(TransactionRefused);
DEV_SIMPLE_EXCEPTION(TransactionAlreadyInChain);
DEV_SIMPLE_EXCEPTION(InconsistentTransactionSha3);
DEV_SIMPLE_EXCEPTION(P2pEnqueueTransactionFailed);

/// state trie related
DEV_SIMPLE_EXCEPTION(InvalidTransactionsRoot);
DEV_SIMPLE_EXCEPTION(InvalidReceiptsStateRoot);
DEV_SIMPLE_EXCEPTION(InvalidAccountStartNonceInState);
DEV_SIMPLE_EXCEPTION(IncorrectAccountStartNonceInState);

/// block && block header related
DEV_SIMPLE_EXCEPTION(InvalidParentHash);
DEV_SIMPLE_EXCEPTION(InvalidNumber);
DEV_SIMPLE_EXCEPTION(UnknownParent);
DEV_SIMPLE_EXCEPTION(InvalidBlockFormat);
DEV_SIMPLE_EXCEPTION(InvalidBlockHeaderItemCount);
DEV_SIMPLE_EXCEPTION(InvalidBlockWithBadStateOrReceipt);
DEV_SIMPLE_EXCEPTION_RLP(ErrorBlockHash);

/// block execution related
DEV_SIMPLE_EXCEPTION(BlockExecutionFailed);

/// sync related
DEV_SIMPLE_EXCEPTION(InvalidBlockDownloadQueuePiorityInput);
DEV_SIMPLE_EXCEPTION(InvalidSyncPeerCreation);

/// common exceptions
DEV_SIMPLE_EXCEPTION(InvalidNonce);
DEV_SIMPLE_EXCEPTION(InvalidSignature);
DEV_SIMPLE_EXCEPTION(InvalidAddress);
DEV_SIMPLE_EXCEPTION(AddressAlreadyUsed);
DEV_SIMPLE_EXCEPTION(InvalidTimestamp);
DEV_SIMPLE_EXCEPTION(InvalidProtocolID);
DEV_SIMPLE_EXCEPTION(EmptySealers);
DEV_SIMPLE_EXCEPTION(MethodNotSupport);

struct VMException : Exception
{
};
#define ETH_SIMPLE_EXCEPTION_VM(X)                                \
    struct X : VMException                                        \
    {                                                             \
        const char* what() const noexcept override { return #X; } \
    }
ETH_SIMPLE_EXCEPTION_VM(BadInstruction);
ETH_SIMPLE_EXCEPTION_VM(BadJumpDestination);
ETH_SIMPLE_EXCEPTION_VM(OutOfGas);
ETH_SIMPLE_EXCEPTION_VM(GasOverflow);
ETH_SIMPLE_EXCEPTION_VM(OutOfStack);
ETH_SIMPLE_EXCEPTION_VM(StackUnderflow);
ETH_SIMPLE_EXCEPTION_VM(DisallowedStateChange);
ETH_SIMPLE_EXCEPTION_VM(BufferOverrun);

struct UnexpectedException : virtual Exception
{
};
#define ETH_UNEXPECTED_EXCEPTION(X)                               \
    struct X : virtual UnexpectedException                        \
    {                                                             \
        const char* what() const noexcept override { return #X; } \
    }
ETH_UNEXPECTED_EXCEPTION(PrecompiledError);

/// Reports VM internal error. This is not based on VMException because it must be handled
/// differently than defined consensus exceptions.
struct InternalVMError : Exception
{
};

struct RevertInstruction : VMException
{
    explicit RevertInstruction(owning_bytes_ref&& _output) : m_output(std::move(_output)) {}
    RevertInstruction(RevertInstruction const&) = delete;
    RevertInstruction(RevertInstruction&&) = default;
    RevertInstruction& operator=(RevertInstruction const&) = delete;
    RevertInstruction& operator=(RevertInstruction&&) = default;

    char const* what() const noexcept override { return "Revert instruction"; }

    owning_bytes_ref&& output() { return std::move(m_output); }

private:
    owning_bytes_ref m_output;
};
}  // namespace eth
}  // namespace dev
