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
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-task/Wait.h"
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/throw_exception.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
std::optional<u256> parsePresentQuantity(std::optional<std::string> const& s)
{
    if (!s || s->empty())
    {
        return std::nullopt;
    }
    if (auto value = safeFromBigQuantity(*s))
    {
        return *value;
    }
    BOOST_THROW_EXCEPTION(std::invalid_argument("invalid quantity: " + *s));
}
}  // namespace

bcos::protocol::Transaction::Ptr CallRequest::takeToTransaction(
    bcos::protocol::TransactionFactory::Ptr const& factory,
    bcos::scheduler::SchedulerInterface::Ptr const& scheduler, std::optional<u256> blockBaseFee)
{
    std::string nonce;
    if (to.empty() && scheduler) [[unlikely]]
    {
        // estimate gas deploy contract
        if (from.has_value())
        {
            if (const auto entry = task::syncWait(scheduler->getPendingStorageAt(
                    bcos::precompiled::trimHexPrefix(from.value()), "nonce", 0)))
            {
                // FISCO stores account nonces as DECIMAL strings (EVMAccount writes
                // convert_to<std::string>(); StorageStateView reads them unprefixed),
                // and the transaction nonce is parsed as HEX downstream — both
                // bcosTransactionToEvmone (safeFromQuantity) and TransactionExecutorImpl
                // (hex2u) treat it as hex. So the stored decimal must be converted to a
                // hex quantity here, otherwise a deployment eth_estimateGas at nonce >= 10
                // gets its decimal "12" misread as hex 0x12 = 18 (NONCE_TOO_HIGH). 0-9
                // coincide in both bases, which is why only the 11th+ deployment would break.
                //
                // The all-digits guard keeps this path safe: bcos::u256 throws on an
                // unparseable string, and an empty or non-numeric stored nonce is left unset
                // (empty nonce string) — a corrupt row falls back to the executor reading the
                // sender's state nonce rather than aborting the RPC.
                if (auto const raw = entry->get();
                    !raw.empty() && std::all_of(raw.begin(), raw.end(),
                                        [](char c) { return c >= '0' && c <= '9'; }))
                {
                    nonce = toQuantity(bcos::u256(raw));
                }
            }
        }
    }
    // The OP-line call pipeline (OpstackExecutor::m_prepare → opValidate) prices every call
    // through the transaction's RLP envelope bytes and rejects an empty envelope with EINVAL
    // ("Invalid argument" at the RPC) — so on OP headers (blockBaseFee present — the same
    // baseFee discriminator gasPrice/feeHistory use; PBFT headers never write the tars field)
    // build the simulation tx via Web3Transaction::takeToTarsTransaction, which stores the
    // envelope in extraTransactionBytes. On PBFT headers the plain BCOS factory tx below is
    // the unchanged pre-envelope behavior — an unconditional rewrite would hand PBFT chains a
    // dummy-signed envelope with a 1-ether/gas default price and break every read-only call
    // whose sender cannot cover it (kyonRay #5496 R1-1). Unsigned legacy shape: chainId stays
    // nullopt (pre-EIP-155 exemption from the node chain-id check), dummy r/s mirror the
    // scheduler-test fixture (the sender is forced below regardless).
    //
    // Pricing-less eth_call: op-geth skips the fee-cap check. We cannot skip it here, so
    // default max_gas_price to max(head.baseFee*2, 2 gwei) — a fixed 2 gwei loses after a
    // few Holocene full blocks. The head baseFee is always available on this path.
    //
    // Explicit malformed quantities must fail closed (InvalidParams upstream): never treat
    // "0xzz" as absent and silently substitute defaults, and never fall back to a plain BCOS
    // factory tx that changes execution semantics.
    if (blockBaseFee.has_value())
    {
        Web3Transaction w3{};
        w3.type = TransactionType::Legacy;
        w3.chainId = std::nullopt;
        if (!to.empty())
        {
            w3.to = Address(to);
        }
        w3.data = data;
        w3.value = parsePresentQuantity(value).value_or(u256(0));
        // Envelope and mirror must agree (the cross-check's premise): the parsed nonce
        // goes INTO the envelope, not just the mirror — an envelope nonce of 0 next to a
        // real mirror nonce is exactly the divergence the PR's trust-boundary narrative
        // rejects, and it makes the deployment nonce fix above moot for anything that
        // reads the envelope (kyonRay #5496 R1-11). Unknown stays 0 (the mirror keeps
        // the empty string; the executor falls back to the sender's state nonce).
        w3.nonce = safeFromQuantity(nonce).value_or(0);
        // op-geth caps a gas-less eth_call at its RPC gas cap; default to the 30M block-gas
        // convention so a pricing-less simulation passes the intrinsic-gas check.
        w3.gasLimit = gas.value_or(30'000'000);
        u256 const defaultFee = std::max(*blockBaseFee * 2, u256(2'000'000'000));
        w3.maxPriorityFeePerGas = parsePresentQuantity(gasPrice).value_or(
            parsePresentQuantity(maxFeePerGas).value_or(defaultFee));
        w3.signatureV = 0;
        w3.signatureR = bcos::bytes(32, 0x01);
        w3.signatureS = bcos::bytes(32, 0x02);
        auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
        // takeToTarsTransaction renders the numeric nonce; restore the passthrough semantics
        // the RPC contract has (converted hex quantity, or empty when unknown — the executor
        // then falls back to the sender's state nonce).
        tarsHolder->data.nonce = nonce;
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsHolder]() { return tarsHolder.get(); });
        if (from.has_value())
        {
            if (auto const sender = safeFromHexWithPrefix(from.value()))
            {
                tx->forceSender(sender.value());
            }
        }
        return tx;
    }
    auto tx = factory->createTransaction(1, std::move(this->to), this->data, nonce, 0, {}, {}, 0,
        "", value.value_or(""), gasPrice.value_or(""), gas.value_or(0), maxFeePerGas.value_or(""),
        maxPriorityFeePerGas.value_or(""));
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
        if (!dataValue->isString())
        {
            return {false, {}};
        }
        auto dataBytes = bcos::safeFromHexWithPrefix(dataValue->asString());
        if (!dataBytes)
        {
            // Present but malformed DATA must fail closed — never silently drop to empty.
            return {false, {}};
        }
        _request.data = std::move(*dataBytes);
    }
    if (const auto* value = _root.find("to"))
    {
        if (!value->isString())
        {
            return {false, {}};
        }
        _request.to = value->asString();
    }
    if (const auto* value = _root.find("from"))
    {
        if (!value->isString())
        {
            return {false, {}};
        }
        _request.from = value->asString();
    }
    if (const auto* value = _root.find("gas"))
    {
        if (!value->isString())
        {
            return {false, {}};
        }
        auto gas = safeFromQuantity(value->asString());
        if (!gas)
        {
            return {false, {}};
        }
        _request.gas = *gas;
    }
    auto takeQuantityField = [&](char const* key, std::optional<std::string>& field) -> bool {
        if (const auto* value = _root.find(key); value != nullptr)
        {
            if (!value->isString())
            {
                return false;
            }
            auto const& raw = value->asString();
            if (!safeFromBigQuantity(raw))
            {
                return false;
            }
            field = raw;
        }
        return true;
    };
    if (!takeQuantityField("gasPrice", _request.gasPrice) ||
        !takeQuantityField("value", _request.value) ||
        !takeQuantityField("maxPriorityFeePerGas", _request.maxPriorityFeePerGas) ||
        !takeQuantityField("maxFeePerGas", _request.maxFeePerGas))
    {
        return {false, {}};
    }
    return {true, std::move(_request)};
}
