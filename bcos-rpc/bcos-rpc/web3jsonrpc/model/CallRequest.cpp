/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file CallRequest.cpp
 * @author: kyonGuo
 * @date 2024/4/11
 */

#include "CallRequest.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-task/Wait.h"
#include <algorithm>

using namespace bcos;
using namespace bcos::rpc;

std::optional<std::string> CallRequest::nonceFromPendingEntry(
    std::optional<bcos::storage::Entry> const& entry)
{
    if (!entry)
    {
        return std::nullopt;
    }
    // FISCO stores account nonces as DECIMAL strings (EVMAccount writes
    // convert_to<std::string>(); StorageStateView reads them unprefixed), and the transaction
    // nonce is parsed as HEX downstream — both bcosTransactionToEvmone (safeFromQuantity) and
    // TransactionExecutorImpl (hex2u) treat it as hex. So the stored decimal must be converted
    // to a hex quantity here, otherwise an eth_estimateGas at nonce >= 10 gets its decimal
    // "12" misread as hex 0x12 = 18 (NONCE_TOO_HIGH). 0-9 coincide in both bases, which is why
    // only the 11th+ transaction would break.
    //
    // The all-digits guard keeps the caller's noexcept contract: bcos::u256 throws on an
    // unparseable string, and an empty or non-numeric stored nonce is left unset (empty nonce
    // string) — a corrupt row falls back to the executor reading the sender's state nonce
    // rather than aborting the RPC. The length bound closes the last throw window: a
    // >=79-digit all-digit row passes the digits guard but overflows u256 (2^256-1 has 78
    // decimal digits), so it must fall back like any other corrupt row instead of throwing
    // inside the RPC handler.
    auto const raw = entry->get();
    constexpr std::size_t c_maxNonceDigits = 78;
    if (raw.empty() || raw.size() > c_maxNonceDigits ||
        !std::all_of(raw.begin(), raw.end(), [](char c) { return c >= '0' && c <= '9'; }))
    {
        return std::nullopt;
    }
    return toQuantity(bcos::u256(raw));
}

bcos::protocol::Transaction::Ptr CallRequest::takeToTransaction(
    bcos::protocol::TransactionFactory::Ptr const& factory, std::optional<std::string> pendingNonce,
    std::optional<uint64_t> chainBlockGasLimit) noexcept
{
    uint64_t gasLimit = gas.value_or(0);
    // eth_estimateGas omits gas; validation rejects gasLimit==0 ("intrinsic gas too low").
    // Cap at the parent block's gas limit when the request did not pin one: absent gas AND an
    // explicit zero both mean "size it for me" — op-geth's estimator does `hi = Header.GasLimit`
    // unless `GasLimit >= params.TxGas`, and EthEndpoint::call's guard reads the header for
    // exactly these two cases. Keeping them on one predicate is what removes the old asymmetry
    // (the guard demanded the header read for gas:"0x0", then the conversion left it at zero).
    // A failed header read leaves the limit at 0 so validation fails instead of being silently
    // sized against a constant that has nothing to do with this chain's configuration. The
    // endpoint supplies the bound on both arms: the estimate arm passes the target block's
    // gasLimit, and the eth_call arm passes the RPC gas cap (bounded by the block limit when
    // its header is readable) — geth sizes an omitted-or-zero eth_call budget the same way.
    if ((!gas.has_value() || *gas == 0) && chainBlockGasLimit.has_value())
    {
        gasLimit = *chainBlockGasLimit;
    }
    // The request is consumed by this call (noexcept, single use), so move the optional
    // strings out instead of value_or's copy (5593 round-3 S).
    auto tx = factory->createTransaction(1, std::move(this->to), std::move(this->data),
        pendingNonce.value_or(std::string{}), 0, {}, {}, 0, "",
        this->value.has_value() ? std::move(*this->value) : std::string{},
        this->gasPrice.has_value() ? std::move(*this->gasPrice) : std::string{}, gasLimit,
        this->maxFeePerGas.has_value() ? std::move(*this->maxFeePerGas) : std::string{},
        this->maxPriorityFeePerGas.has_value() ? std::move(*this->maxPriorityFeePerGas) :
                                                 std::string{});
    if (from.has_value())
    {
        if (auto const sender = safeFromHexWithPrefix(from.value()))
        {
            tx->forceSender(sender.value());
        }
    }
    return tx;
}


std::tuple<bool, CallRequest> rpc::decodeCallRequest(Json::Value const& _root)
{
    CallRequest _request;
    if (!_root.isObject())
    {
        return {false, _request};
    }
    const auto* dataValue = _root.find("data");
    if (dataValue == nullptr)
    {
        dataValue = _root.find("input");
    }
    if (dataValue != nullptr)
    {
        if (auto dataBytes = bcos::safeFromHexWithPrefix(dataValue->asString()))
        {
            _request.data = std::move(*dataBytes);
        }
    }
    if (const auto* value = _root.find("to"))
    {
        _request.to = value->asString();
    }
    if (const auto* value = _root.find("from"))
    {
        _request.from = value->asString();
    }
    if (const auto* value = _root.find("gas"))
    {
        _request.gas = fromQuantity(value->asString());
    }
    if (const auto* value = _root.find("gasPrice"))
    {
        _request.gasPrice = value->asString();
    }
    if (const auto* value = _root.find("value"))
    {
        _request.value = value->asString();
    }
    if (const auto* value = _root.find("maxPriorityFeePerGas"))
    {
        _request.maxPriorityFeePerGas = value->asString();
    }
    if (const auto* value = _root.find("maxFeePerGas"))
    {
        _request.maxFeePerGas = value->asString();
    }
    return {true, std::move(_request)};
}