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
 * @file NewPayloadV3.cpp
 * @date 2026/5/21
 */

#include "NewPayloadV3.h"

namespace bcos::rpc
{
std::tuple<bool, NewPayloadV3Request> decodeNewPayloadV3Request(Json::Value const& _params)
{
    NewPayloadV3Request request;
    if (!_params.isArray() || _params.size() != 3)
    {
        return {false, request};
    }

    if (!decodeExecutionPayloadV3(_params[0], request.executionPayload))
    {
        return {false, request};
    }
    if (!_params[1].isArray())
    {
        return {false, request};
    }
    request.expectedBlobVersionedHashes.reserve(_params[1].size());
    for (auto const& item : _params[1])
    {
        if (!item.isString())
        {
            return {false, request};
        }
        request.expectedBlobVersionedHashes.emplace_back(item.asString());
    }
    if (!_params[2].isString())
    {
        return {false, request};
    }
    request.parentBeaconBlockRoot = _params[2].asString();
    return {true, std::move(request)};
}

void combineNewPayloadV3Response(Json::Value& _result, NewPayloadV3Response const& _response)
{
    appendPayloadStatus(_result, _response.payloadStatus);
}
}  // namespace bcos::rpc

