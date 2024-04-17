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
 * @file JsonValidator.cpp
 * @author: kyonGuo
 * @date 2024/3/28
 */

#include "JsonValidator.h"

namespace bcos::rpc
{
std::tuple<bool, std::string> JsonValidator::validate(const Json::Value& root)
{
    if (auto const result{checkRequestFields(root)}; !std::get<0>(result))
    {
        return result;
    }
    // check params specific https://github.com/ethereum/execution-apis
    return {true, ""};
}

std::tuple<bool, std::string> JsonValidator::checkRequestFields(const Json::Value& root)
{
    // flag for 4 fileds, maybe twice?
    auto flag = 0x1111;
    for (auto item = root.begin(); item != root.end(); item++)
    {
        if (item.name() == "jsonrpc")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0x1110;
        }
        else if (item.name() == "method")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0x1101;
        }
        else if (item.name() == "params")
        {
            if (!item->isArray())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0x1011;
        }
        else if (item.name() == "id")
        {
            if (!item->isUInt64())
            {
                if (item->isString())
                {
                    if (std::string idString = item->asString();
                        !std::regex_match(idString, std::regex("^[0-9a-fA-F-]+$")))
                    {
                        return {false, "Invalid field: " + item.name()};
                    }
                }
                else
                {
                    return {false, "Invalid field: " + item.name()};
                }
            }
            flag &= 0x0111;
        }
        else
        {
            return {false, "Invalid field: " + item.name()};
        }
    }
    if (flag != 0)
    {
        return {false, "Request not valid, required fields: jsonrpc, method, params, id"};
    }
    return {true, ""};
}

}  // namespace bcos::rpc