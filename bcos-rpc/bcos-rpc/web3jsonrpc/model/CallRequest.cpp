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

using namespace bcos;
using namespace bcos::rpc;

bcos::protocol::Transaction::Ptr CallRequest::takeToTransaction(
    bcos::protocol::TransactionFactory::Ptr const& factory) noexcept
{
    auto tx = factory->createTransaction(1, std::move(this->to), this->data, "", 0, {}, {}, 0, "",
        value.value_or(""), gasPrice.value_or(""), gas.value_or(0), maxFeePerGas.value_or(""),
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