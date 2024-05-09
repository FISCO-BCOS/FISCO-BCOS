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
 * @file FilterRequest.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/filter/Common.h>
#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::rpc
{

class FilterRequest
{
public:
    using Ptr = std::shared_ptr<FilterRequest>;
    using ConstPtr = std::shared_ptr<const FilterRequest>;

    FilterRequest() = default;
    virtual ~FilterRequest() = default;

    FilterRequest(const FilterRequest& other)
    {
        if (&other == this)
        {
            return;
        }
        m_fromBlock = other.m_fromBlock;
        m_toBlock = other.m_toBlock;
        m_fromIsLatest = other.m_fromIsLatest;
        m_toIsLatest = other.m_toIsLatest;
        m_addresses = other.m_addresses;
        m_topics = other.m_topics;
        m_blockHash = other.m_blockHash;
    }

public:
    int64_t fromBlock() const { return m_fromBlock; }
    int64_t toBlock() const { return m_toBlock; }
    const std::set<std::string>& addresses() const { return m_addresses; }
    std::set<std::string>& addresses() { return m_addresses; }
    const std::vector<std::set<std::string>>& topics() const { return m_topics; }
    std::vector<std::set<std::string>>& topics() { return m_topics; }
    std::string& blockHash() { return m_blockHash; }
    const std::string& blockHash() const { return m_blockHash; }
    bool fromIsLatest() const { return m_fromIsLatest; }
    bool toIsLatest() const { return m_toIsLatest; }

    void setFromBlock(int64_t _fromBlock) { m_fromBlock = _fromBlock; }
    void setToBlock(int64_t _toBlock) { m_toBlock = _toBlock; }
    void addAddress(std::string _address) { m_addresses.insert(std::move(_address)); }
    void resizeTopic(size_t size) { m_topics.resize(size); }
    void addTopic(std::size_t _index, std::string _topic)
    {
        m_topics[_index].insert(std::move(_topic));
    }
    void setBlockHash(const std::string& _hash) { m_blockHash = _hash; }
    void fromJson(const Json::Value& jParams, protocol::BlockNumber latest = 0);
    bool checkBlockRange();

    virtual int32_t InvalidParamsCode() = 0;

protected:
    bcos::protocol::BlockNumber m_fromBlock = 0;
    bcos::protocol::BlockNumber m_toBlock = 0;
    bool m_fromIsLatest = true;
    bool m_toIsLatest = true;
    std::set<std::string> m_addresses;
    std::vector<std::set<std::string>> m_topics;
    std::string m_blockHash;
};

class FilterRequestFactory
{
public:
    using Ptr = std::shared_ptr<FilterRequestFactory>;
    FilterRequestFactory() = default;
    virtual ~FilterRequestFactory() = default;
    virtual FilterRequest::Ptr create() = 0;
    virtual FilterRequest::Ptr create(const FilterRequest& req) = 0;
};

}  // namespace bcos::rpc