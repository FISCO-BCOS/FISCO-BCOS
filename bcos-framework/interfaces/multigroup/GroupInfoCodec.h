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
 * @brief the information used to deploy new node
 * @file GroupInfoCodec.h
 * @author: yujiechen
 * @date 2022-03-29
 */
#pragma once
#include "GroupInfo.h"
#include <json/value.h>
#include <memory>

namespace bcos
{
namespace group
{
class GroupInfoCodec
{
public:
    using Ptr = std::shared_ptr<GroupInfoCodec>;
    GroupInfoCodec() = default;
    virtual ~GroupInfoCodec() {}

    virtual GroupInfo::Ptr deserialize(const std::string& _encodedData) = 0;
    virtual Json::Value serialize(GroupInfo::Ptr _groupInfo) = 0;
};
}  // namespace group
}  // namespace bcos