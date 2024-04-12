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
 * @file CallRequest.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::rpc
{
struct CallRequest
{
    // Address
    std::optional<std::string> from{};
    // Address
    std::string to{};
    bcos::bytes data{};
    // Quantity
    std::optional<uint64_t> gas{};
    // Quantity
    std::optional<std::string> gasPrice{};
    // Quantity
    std::optional<std::string> value{};
    std::optional<std::string> maxPriorityFeePerGas{};
    std::optional<std::string> maxFeePerGas{};


    friend std::ostream& operator<<(std::ostream& _out, const CallRequest& _in)
    {
        _out << "from: " << _in.from.value_or("null") << ", ";
        _out << "to: " << _in.to << ", ";
        _out << "data: " << bcos::toHex(_in.data) << ", ";
        _out << "gas: " << _in.gas.value_or(0) << ", ";
        _out << "gasPrice: " << _in.gasPrice.value_or("") << ", ";
        _out << "value: " << _in.value.value_or("") << ", ";
        _out << "maxPriorityFeePerGas: " << _in.maxPriorityFeePerGas.value_or("") << ", ";
        _out << "maxFeePerGas: " << _in.maxFeePerGas.value_or("") << ", ";
        return _out;
    }
    bcos::protocol::Transaction::Ptr takeToTransaction(
        bcos::protocol::TransactionFactory::Ptr const&) noexcept;
};
[[maybe_unused]] std::tuple<bool, CallRequest> decodeCallRequest(Json::Value const& _root) noexcept;
}  // namespace bcos::rpc
