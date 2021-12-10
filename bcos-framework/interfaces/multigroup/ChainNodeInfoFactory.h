/**
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
 * @brief factory to build the ChainNodeInfo
 * @file ChainNodeInfoFactory.h
 * @author: yujiechen
 * @date 2021-09-18
 */
#pragma once
#include "ChainNodeInfo.h"
namespace bcos
{
namespace group
{
class ChainNodeInfoFactory
{
public:
    using Ptr = std::shared_ptr<ChainNodeInfoFactory>;
    ChainNodeInfoFactory() = default;
    virtual ~ChainNodeInfoFactory() {}
    virtual ChainNodeInfo::Ptr createNodeInfo() { return std::make_shared<ChainNodeInfo>(); }
    virtual ChainNodeInfo::Ptr createNodeInfo(std::string const& _nodeInfoJson)
    {
        return std::make_shared<ChainNodeInfo>(_nodeInfoJson);
    }
    virtual ChainNodeInfo::Ptr createNodeInfo(std::string const& _nodeName, int32_t _type)
    {
        return std::make_shared<ChainNodeInfo>(_nodeName, _type);
    }
};
}  // namespace group
}  // namespace bcos