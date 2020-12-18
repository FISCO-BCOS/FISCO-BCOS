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
#include <libutilities/Common.h>
#include <libutilities/Exceptions.h>

namespace bcos
{
namespace protocol
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
DERIVE_BCOS_EXCEPTION(OutOfGasBase);
DERIVE_BCOS_EXCEPTION(OutOfGasIntrinsic);
DERIVE_BCOS_EXCEPTION(NotEnoughCash);
DERIVE_BCOS_EXCEPTION(GasPriceTooLow);
DERIVE_BCOS_EXCEPTION(BlockGasLimitReached);
DERIVE_BCOS_EXCEPTION(TooMuchGasUsed);
DERIVE_BCOS_EXCEPTION(InvalidGasUsed);
DERIVE_BCOS_EXCEPTION(InvalidGasLimit);

/// DB related exceptions
DERIVE_BCOS_EXCEPTION(NotEnoughAvailableSpace);
DERIVE_BCOS_EXCEPTION(ExtraDataTooBig);
DERIVE_BCOS_EXCEPTION(ExtraDataIncorrect);
DERIVE_BCOS_EXCEPTION(DatabaseAlreadyOpen);
DERIVE_BCOS_EXCEPTION(PermissionDenied);

/// transaction releated exceptions
DERIVE_BCOS_EXCEPTION(InvalidTransactionFormat);
DERIVE_BCOS_EXCEPTION(TransactionIsUnsigned);
DERIVE_BCOS_EXCEPTION(TransactionRefused);
DERIVE_BCOS_EXCEPTION(TransactionAlreadyInChain);
DERIVE_BCOS_EXCEPTION(InvalidTransaction);
DERIVE_BCOS_EXCEPTION(P2pEnqueueTransactionFailed);

/// state trie related
DERIVE_BCOS_EXCEPTION(InvalidTransactionsRoot);
DERIVE_BCOS_EXCEPTION(InvalidReceiptsStateRoot);
DERIVE_BCOS_EXCEPTION(InvalidAccountStartNonceInState);
DERIVE_BCOS_EXCEPTION(IncorrectAccountStartNonceInState);

/// block && block header related
DERIVE_BCOS_EXCEPTION(InvalidParentHash);
DERIVE_BCOS_EXCEPTION(InvalidNumber);
DERIVE_BCOS_EXCEPTION(UnknownParent);
DERIVE_BCOS_EXCEPTION(InvalidBlockFormat);
DERIVE_BCOS_EXCEPTION(InvalidBlockHeaderItemCount);
DERIVE_BCOS_EXCEPTION(InvalidBlockWithBadStateOrReceipt);
DERIVE_BCOS_EXCEPTION(ErrorBlockHash);

/// block execution related
DERIVE_BCOS_EXCEPTION(BlockExecutionFailed);

/// sync related
DERIVE_BCOS_EXCEPTION(InvalidBlockDownloadQueuePiorityInput);
DERIVE_BCOS_EXCEPTION(InvalidSyncPeerCreation);

/// common exceptions
DERIVE_BCOS_EXCEPTION(InvalidNonce);
DERIVE_BCOS_EXCEPTION(InvalidSignature);
DERIVE_BCOS_EXCEPTION(InvalidAddress);
DERIVE_BCOS_EXCEPTION(AddressAlreadyUsed);
DERIVE_BCOS_EXCEPTION(InvalidTimestamp);
DERIVE_BCOS_EXCEPTION(InvalidProtocolID);
DERIVE_BCOS_EXCEPTION(EmptySealers);
DERIVE_BCOS_EXCEPTION(MethodNotSupport);

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
}  // namespace protocol
}  // namespace bcos
