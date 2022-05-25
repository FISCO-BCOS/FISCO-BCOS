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
 * @file RouterTableInterface.h
 * @author: yujiechen
 * @date 2022-5-24
 */
#pragma once
#include <bcos-utilities/Common.h>
#include <memory>
namespace bcos
{
namespace gateway
{
class RouterTableEntryInterface
{
public:
    using Ptr = std::shared_ptr<RouterTableEntryInterface>;
    RouterTableEntryInterface() = default;
    virtual ~RouterTableEntryInterface() {}

    virtual void setDstNode(std::string const& _dstNode) = 0;
    virtual void setNextHop(std::string const& _nextHop) = 0;
    virtual void setTTL(int32_t _ttl) = 0;
    virtual void incTTL(int32_t _deltaTTL) = 0;

    virtual std::string const& dstNode() const = 0;
    virtual std::string const& nextHop() const = 0;
    virtual int32_t ttl() const = 0;
};

class RouterTableInterface
{
public:
    using Ptr = std::shared_ptr<RouterTableInterface>;
    RouterTableInterface() = default;
    virtual ~RouterTableInterface() {}

    virtual bool update(std::set<std::string>& _unreachableNodes, std::string const& _generatedFrom,
        RouterTableEntryInterface::Ptr _entry) = 0;
    virtual bool erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID) = 0;

    virtual std::map<std::string, RouterTableEntryInterface::Ptr> const& routerEntries() = 0;

    virtual void setNodeID(std::string const& _nodeID) = 0;
    virtual std::string const& nodeID() const = 0;
    virtual void setUnreachableTTL(int _unreachableTTL) = 0;
    virtual std::string getNextHop(std::string const& _nodeID) = 0;
    virtual std::set<std::string> getAllReachableNode() = 0;

    virtual void encode(bcos::bytes& _encodedData) = 0;
    virtual void decode(bcos::bytesConstRef _decodedData) = 0;
};

class RouterTableFactory
{
public:
    using Ptr = std::shared_ptr<RouterTableFactory>;
    RouterTableFactory() = default;
    virtual ~RouterTableFactory() {}

    virtual RouterTableInterface::Ptr createRouterTable() = 0;
    virtual RouterTableInterface::Ptr createRouterTable(bcos::bytesConstRef _decodedData) = 0;
    virtual RouterTableEntryInterface::Ptr createRouterEntry() = 0;
};

}  // namespace gateway
}  // namespace bcos