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
 * @file BlockNumberInfo.h
 * @author: octopus
 * @date 2021-10-04
 */

#pragma once

#include <boost/thread/thread.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bcos
{
namespace cppsdk
{
namespace service
{
class BlockNumberInfo
{
public:
    using Ptr = std::shared_ptr<BlockNumberInfo>;
    using ConstPtr = std::shared_ptr<const BlockNumberInfo>;

public:
    std::string group() const { return m_group; }
    void setGroup(const std::string& _group) { m_group = _group; }
    std::string node() const { return m_node; }
    void setNode(const std::string& _node) { m_node = _node; }
    int64_t blockNumber() const { return m_blockNumber; }
    void setBlockNumber(int64_t _blockNumber) { m_blockNumber = _blockNumber; }

public:
    std::string toJson();
    bool fromJson(const std::string& _json);

private:
    std::string m_group;
    std::string m_node;
    int64_t m_blockNumber;
};

}  // namespace service
}  // namespace cppsdk
}  // namespace bcos
