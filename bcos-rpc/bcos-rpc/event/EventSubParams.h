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
 * @file EvenSubParams.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/event/Common.h>
#include <string>
#include <vector>

namespace bcos
{
namespace event
{
class EventSubParams
{
public:
    using Ptr = std::shared_ptr<EventSubParams>;
    using ConstPtr = std::shared_ptr<const EventSubParams>;

public:
    int64_t fromBlock() const { return m_fromBlock; }
    int64_t toBlock() const { return m_toBlock; }
    const std::set<std::string>& addresses() const { return m_addresses; }
    std::set<std::string>& addresses() { return m_addresses; }
    const std::vector<std::set<std::string>>& topics() const { return m_topics; }
    std::vector<std::set<std::string>>& topics() { return m_topics; }

    void setFromBlock(int64_t _fromBlock) { m_fromBlock = _fromBlock; }
    void setToBlock(int64_t _toBlock) { m_toBlock = _toBlock; }
    void addAddress(const std::string& _address) { m_addresses.insert(_address); }
    bool addTopic(std::size_t _index, const std::string& _topic)
    {
        if (_index >= EVENT_LOG_TOPICS_MAX_INDEX)
        {
            return false;
        }

        m_topics.resize(_index + 1);
        m_topics[_index].insert(_topic);
        return true;
    }

private:
    bcos::protocol::BlockNumber m_fromBlock = -1;
    bcos::protocol::BlockNumber m_toBlock = -1;
    std::set<std::string> m_addresses;
    std::vector<std::set<std::string>> m_topics;
};

}  // namespace event
}  // namespace bcos