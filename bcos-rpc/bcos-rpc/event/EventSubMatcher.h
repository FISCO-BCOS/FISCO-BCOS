/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file EvenPushMatcher.h
 * @author: octopus
 * @date 2021-09-10
 */
#pragma once
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-rpc/event/EventSubParams.h>
#include <json/json.h>

namespace bcos::event
{
class EventSubMatcher
{
public:
    using Ptr = std::shared_ptr<EventSubMatcher>;
    using ConstPtr = std::shared_ptr<const EventSubMatcher>;

    virtual ~EventSubMatcher() = default;

    virtual bool matches(
        EventSubParams::ConstPtr _params, const bcos::protocol::LogEntry& _logEntry);

    uint32_t matches(EventSubParams::ConstPtr _params,
        const bcos::protocol::TransactionReceipt& _receipt, const bcos::protocol::Transaction& _tx,
        std::size_t _txIndex, Json::Value& _result);
    uint32_t matches(EventSubParams::ConstPtr _params, bcos::protocol::Block::ConstPtr _block,
        Json::Value& _result);
};

}  // namespace bcos::event
