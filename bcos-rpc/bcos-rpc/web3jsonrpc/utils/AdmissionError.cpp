/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file AdmissionError.cpp
 * @author: kyonGuo
 * @date 2026/9/9
 */

#include "AdmissionError.h"
#include "Common.h"
#include <array>
#include <string>

namespace bcos::rpc
{
namespace
{
using TS = protocol::TransactionStatus;

struct Entry
{
    TS status;
    int32_t code;
    std::string_view message;
};

// Every status that answers with something other than (-32000, its own name); an empty message
// means the status name. The words are go-ethereum's: core/error.go, core/txpool/errors.go,
// core/types/transaction.go and transaction_signing.go, core/vm/errors.go (the initcode size),
// core/txpool/legacypool/legacypool.go.
constexpr auto c_table = std::to_array<Entry>({
    // Not a transaction this node can accept as-is.
    {TS::Malformed, InvalidParams, ""},
    {TS::InvalidSignature, InvalidParams, "invalid sender"},
    {TS::InvalidChainId, InvalidParams, "invalid chain id for signer"},
    // The node's fault.
    {TS::Unknown, InternalError, "admission could not be decided"},
    // Well-formed, refused by a rule geth has words for.
    {TS::InsufficientFunds, Web3DefaultError, "insufficient funds for gas * price + value"},
    {TS::AlreadyInTxPool, Web3DefaultError, "already known"},
    {TS::TxPoolIsFull, Web3DefaultError, "txpool is full"},
    {TS::OutOfGasLimit, Web3DefaultError, "intrinsic gas too low"},
    {TS::TipGreaterThanFeeCap, Web3DefaultError,
        "max priority fee per gas higher than max fee per gas"},
    {TS::FeeCapLessThanBaseFee, Web3DefaultError, "max fee per gas less than block base fee"},
    {TS::TxTypeNotSupported, Web3DefaultError, "transaction type not supported"},
    {TS::BlobTxNotAllowed, Web3DefaultError, "transaction type not supported"},
    {TS::SenderNoEOA, Web3DefaultError, "sender not an eoa"},
    {TS::NonceHasMaxValue, Web3DefaultError, "nonce has max value"},
    {TS::MaxInitCodeSizeExceeded, Web3DefaultError, "max initcode size exceeded"},
    {TS::CreateSetCodeTx, Web3DefaultError,
        "EIP-7702 transaction cannot be used to create contract"},
    {TS::EmptyAuthorizationList, Web3DefaultError, "EIP-7702 transaction with empty auth list"},
});
}  // namespace

JsonRpcException admissionError(protocol::TransactionStatus status)
{
    for (auto const& entry : c_table)
    {
        if (entry.status == status)
        {
            return {entry.code,
                entry.message.empty() ? protocol::toString(status) : std::string(entry.message)};
        }
    }
    return {Web3DefaultError, protocol::toString(status)};
}

JsonRpcException admissionError(protocol::TransactionStatus status, std::string_view detail)
{
    auto base = admissionError(status);
    return {base.code(), base.msg() + " (" + std::string(detail) + ")"};
}
}  // namespace bcos::rpc
