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
 * @file Log.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once

#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>

namespace bcos::rpc
{
struct Log
{
    bool removed = false;
    bcos::bytes address{};
    h256s topics{};
    bcos::bytes data{};
    protocol::BlockNumber number{0};
    crypto::HashType blockHash{};
    crypto::HashType transactionHash{};
    uint32_t txIndex{0};
    uint32_t logIndex{0};

    friend std::ostream& operator<<(std::ostream& _out, const Log& _in)
    {
        _out << "address: " << toHex(_in.address) << ", ";
        _out << "topics: [";
        for (auto& topic : _in.topics)
        {
            _out << topic.hex() << ", ";
        }
        _out << "], ";
        _out << "data: " << bcos::toHex(_in.data) << ", ";
        _out << "number: " << _in.number << ", ";
        _out << "blockHash: " << _in.blockHash.hex() << ", ";
        _out << "transactionHash: " << _in.transactionHash.hex() << ", ";
        _out << "txIndex: " << _in.txIndex << ", ";
        _out << "logIndex: " << _in.logIndex << ", ";
        return _out;
    }
};
using Logs = std::vector<Log>;
[[maybe_unused]] static std::ostream& operator<<(std::ostream& _out, const Logs& _in)
{
    for (const auto& log : _in)
    {
        _out << log;
    }
    return _out;
}
}  // namespace bcos::rpc
