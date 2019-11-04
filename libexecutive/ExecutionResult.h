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
/** @file ExecutionResult.h
 * @author
 * @date
 */

#pragma once

#include <libdevcore/RLP.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/Common.h>
#include <libethcore/Exceptions.h>


namespace dev
{
namespace eth
{
struct VMException;
}

namespace executive
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
    AlreadyKnown = 10000,  /// txPool related errors
    AlreadyInChain = 10001,
    InvalidChainIdOrGroupId = 10002,
    NotBelongToTheGroup = 10003,
    MalformedTx = 10004
};

enum class CodeDeposit
{
    None = 0,
    Failed,
    Success
};

TransactionException toTransactionException(Exception const& _e);
std::ostream& operator<<(std::ostream& _out, TransactionException const& _er);

/// Description of the result of executing a transaction.
struct ExecutionResult
{
    u256 gasUsed = 0;
    TransactionException excepted = TransactionException::Unknown;
    Address newAddress;
    bytes output;
    CodeDeposit codeDeposit =
        CodeDeposit::None;  ///< Failed if an attempted deposit failed due to lack of gas.
    u256 gasRefunded = 0;
    unsigned depositSize = 0;  ///< Amount of code of the creation's attempted deposit.
    u256 gasForDeposit;        ///< Amount of gas remaining for the code deposit phase.

    void reset()
    {
        gasUsed = 0;
        excepted = TransactionException::Unknown;
        newAddress = Address();
        output.clear();
        codeDeposit =
            CodeDeposit::None;  ///< Failed if an attempted deposit failed due to lack of gas.
        gasRefunded = 0;
        depositSize = 0;  ///< Amount of code of the creation's attempted deposit.
        gasForDeposit = 0;
    }
};

std::ostream& operator<<(std::ostream& _out, ExecutionResult const& _er);

}  // namespace executive
}  // namespace dev
