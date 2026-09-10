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
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>

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
//
// The rows ascend by TransactionStatus value -- which is also, today, the order the enum declares
// them in -- because admissionError binary-searches them; the static_assert below holds a new row
// to that ascent and rejects a second row for a status, which a linear scan would have left
// silently unreachable. Which code a status gets, and why, is the code column and the header's
// doc comment -- not the row order.
constexpr auto c_table = std::to_array<Entry>({
    {TS::Unknown, InternalError, "admission could not be decided"},
    {TS::OutOfGasLimit, Web3DefaultError, "intrinsic gas too low"},
    {TS::TxPoolIsFull, Web3DefaultError, "txpool is full"},
    {TS::Malformed, InvalidParams, ""},
    {TS::AlreadyInTxPool, Web3DefaultError, "already known"},
    {TS::InvalidChainId, InvalidParams, "invalid chain id for signer"},
    {TS::InvalidSignature, InvalidParams, "invalid sender"},
    {TS::MaxInitCodeSizeExceeded, Web3DefaultError, "max initcode size exceeded"},
    {TS::SenderNoEOA, Web3DefaultError, "sender not an eoa"},
    {TS::InsufficientFunds, Web3DefaultError, "insufficient funds for gas * price + value"},
    {TS::BlobTxNotAllowed, Web3DefaultError, "transaction type not supported"},
    {TS::TxTypeNotSupported, Web3DefaultError, "transaction type not supported"},
    {TS::TipGreaterThanFeeCap, Web3DefaultError,
        "max priority fee per gas higher than max fee per gas"},
    {TS::CreateSetCodeTx, Web3DefaultError,
        "EIP-7702 transaction cannot be used to create contract"},
    {TS::EmptyAuthorizationList, Web3DefaultError, "EIP-7702 transaction with empty auth list"},
    {TS::NonceHasMaxValue, Web3DefaultError, "nonce has max value"},
    {TS::FeeCapLessThanBaseFee, Web3DefaultError, "max fee per gas less than block base fee"},
});
static_assert(std::ranges::adjacent_find(c_table, std::ranges::greater_equal{}, &Entry::status) ==
                  c_table.end(),
    "c_table must be strictly increasing in TransactionStatus: admissionError binary-searches it");
}  // namespace

JsonRpcException admissionError(protocol::TransactionStatus status)
{
    auto const entry = std::ranges::lower_bound(c_table, status, {}, &Entry::status);
    if (entry == c_table.end() || entry->status != status)
    {
        return {Web3DefaultError, protocol::toString(status)};
    }
    return {entry->code,
        entry->message.empty() ? protocol::toString(status) : std::string(entry->message)};
}

JsonRpcException admissionError(protocol::TransactionStatus status, std::string_view detail)
{
    auto base = admissionError(status);
    return {base.code(), base.msg() + " (" + std::string(detail) + ")"};
}

bool isAdmissionVerdict(int64_t errorCode)
{
    // Error carries its code in an int64_t, TransactionStatus is an int32_t: a value that does
    // not fit is not one of them, and must not become one by truncation.
    if (std::cmp_less(errorCode, std::numeric_limits<int32_t>::min()) ||
        std::cmp_greater(errorCode, std::numeric_limits<int32_t>::max()))
    {
        return false;
    }
    auto const status = static_cast<protocol::TransactionStatus>(errorCode);
    if (status == TS::None)
    {
        return false;
    }
    // toString's switch ends in `case Unknown: default:`, so every value the enum does not
    // declare comes back as "Unknown". That makes "this node has a name for it" the test for a
    // declared status, with Unknown itself the one value that has to be named here -- and it
    // reuses the enum's own switch instead of keeping a second copy of the enumerators in sync.
    // What reusing it costs: a new enumerator whose case someone forgets to add to that switch
    // is read here as a fault, and answered -32603 rather than -32000 with its name.
    return status == TS::Unknown || protocol::toString(status) != "Unknown";
}
}  // namespace bcos::rpc
