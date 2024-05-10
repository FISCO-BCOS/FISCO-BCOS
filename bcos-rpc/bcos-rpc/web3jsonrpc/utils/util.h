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
 * @file util.h
 * @author: kyonGuo
 * @date 2024/3/29
 */

#pragma once
#include <bcos-rpc/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::rpc
{
void buildJsonContent(Json::Value& result, Json::Value& response);
void buildJsonError(
    Json::Value const& request, int32_t code, std::string message, Json::Value& response);
void buildJsonErrorWithData(
    Json::Value& data, int32_t code, std::string message, Json::Value& response);
inline auto printJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}
inline std::string_view toView(const Json::Value& value)
{
    const char* begin = nullptr;
    const char* end = nullptr;
    if (!value.getString(&begin, &end))
    {
        return {};
    }
    std::string_view view(begin, end - begin);
    return view;
}
}  // namespace bcos::rpc
