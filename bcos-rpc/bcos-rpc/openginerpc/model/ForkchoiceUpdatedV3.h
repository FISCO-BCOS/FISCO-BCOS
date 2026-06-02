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
 * @file ForkchoiceUpdatedV3.h
 * @date 2026/5/21
 */

#pragma once

#include "EngineTypes.h"
#include <json/json.h>
#include <optional>
#include <string>
#include <tuple>

namespace bcos::rpc
{
struct ForkchoiceUpdatedV3Request
{
    ForkchoiceState forkchoiceState;
    std::optional<PayloadAttributesV3> payloadAttributes;
};

struct ForkchoiceUpdatedV3Response
{
    PayloadStatus payloadStatus;
    std::optional<std::string> payloadId;
};

[[nodiscard]] std::tuple<bool, ForkchoiceUpdatedV3Request> decodeForkchoiceUpdatedV3Request(
    Json::Value const& _params);
void combineForkchoiceUpdatedV3Response(Json::Value& _result,
    ForkchoiceUpdatedV3Response const& _response);
}  // namespace bcos::rpc

