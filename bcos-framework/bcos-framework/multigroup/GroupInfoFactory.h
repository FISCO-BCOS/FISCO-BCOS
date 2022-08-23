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
 * @brief factory to build the GroupInfo
 * @file GroupInfoFactory.h
 * @author: yujiechen
 * @date 2021-09-18
 */
#pragma once
#include "ChainNodeInfoFactory.h"
#include "GroupInfo.h"
namespace bcos
{
namespace group
{
class GroupInfoFactory
{
public:
    using Ptr = std::shared_ptr<GroupInfoFactory>;
    GroupInfoFactory() = default;
    virtual ~GroupInfoFactory() {}
    virtual GroupInfo::Ptr createGroupInfo() { return std::make_shared<GroupInfo>(); }
    virtual GroupInfo::Ptr createGroupInfo(std::string const& _chainID, std::string const& _groupID)
    {
        return std::make_shared<GroupInfo>(_chainID, _groupID);
    }
};
}  // namespace group
}  // namespace bcos