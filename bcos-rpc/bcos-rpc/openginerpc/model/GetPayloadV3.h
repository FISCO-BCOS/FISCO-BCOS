/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file GetPayloadV3.h
 * @date 2026/5/21
 */

#pragma once

#include "EngineTypes.h"
#include <json/json.h>
#include <string>
#include <tuple>

namespace bcos::rpc
{
struct GetPayloadV3Request
{
    std::string payloadId;
};

struct GetPayloadV3Response
{
    ExecutionPayloadV3 executionPayload;
    std::string blockValue;
    BlobsBundle blobsBundle;
    bool shouldOverrideBuilder{false};
};

[[nodiscard]] std::tuple<bool, GetPayloadV3Request> decodeGetPayloadV3Request(
    Json::Value const& _params);
void combineGetPayloadV3Response(Json::Value& _result, GetPayloadV3Response const& _response);
}  // namespace bcos::rpc

