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
    auto tx = factory->createTransaction(0, std::move(this->to), std::move(this->data), "", 0, {},
        {}, 0, "", value.value_or(""), gasPrice.value_or(""), gas.value_or(0),
        maxFeePerGas.value_or(""), maxPriorityFeePerGas.value_or(""));
    if (from.has_value())
    {
        auto sender = fromHexWithPrefix(from.value());
        tx->forceSender(std::move(sender));
    }
    return tx;
}


std::tuple<bool, CallRequest> rpc::decodeCallRequest(Json::Value const& _root) noexcept
{
    CallRequest _request;
    if (!_root.isObject())
    {
        return {false, _request};
    }
    if (!_root.isMember("data"))
    {
        return {false, _request};
    }
    _request.to = _root.isMember("to") ? _root["to"].asString() : "";
    _request.data = bcos::fromHexWithPrefix(_root["data"].asString());
    if (_root.isMember("from"))
    {
        _request.from = _root["from"].asString();
    }
    if (_root.isMember("gas"))
    {
        _request.gas = fromQuantity(_root["gas"].asString());
    }
    if (_root.isMember("gasPrice"))
    {
        _request.gasPrice = _root["gasPrice"].asString();
    }
    if (_root.isMember("value"))
    {
        _request.value = _root["value"].asString();
    }
    if (_root.isMember("maxPriorityFeePerGas"))
    {
        _request.maxPriorityFeePerGas = _root["maxPriorityFeePerGas"].asString();
    }
    if (_root.isMember("maxFeePerGas"))
    {
        _request.maxFeePerGas = _root["maxFeePerGas"].asString();
    }
    return {true, std::move(_request)};
}