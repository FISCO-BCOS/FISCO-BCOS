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
 * @file ForkchoiceUpdatedV3.cpp
 * @date 2026/5/21
 */

#include "ForkchoiceUpdatedV3.h"

namespace bcos::rpc
{
std::tuple<bool, ForkchoiceUpdatedV3Request> decodeForkchoiceUpdatedV3Request(
    Json::Value const& _params)
{
    ForkchoiceUpdatedV3Request request;
    if (!_params.isArray() || _params.size() < 1 || _params.size() > 2)
    {
        return {false, request};
    }

    if (!decodeForkchoiceState(_params[0], request.forkchoiceState))
    {
        return {false, request};
    }

    if (_params.size() > 1 && !_params[1].isNull())
    {
        PayloadAttributesV3 payloadAttributes;
        if (!decodePayloadAttributesV3(_params[1], payloadAttributes))
        {
            return {false, request};
        }
        request.payloadAttributes = std::move(payloadAttributes);
    }

    return {true, std::move(request)};
}

void combineForkchoiceUpdatedV3Response(
    Json::Value& _result, ForkchoiceUpdatedV3Response const& _response)
{
    Json::Value payloadStatus(Json::objectValue);
    appendPayloadStatus(payloadStatus, _response.payloadStatus);
    _result["payloadStatus"] = std::move(payloadStatus);
    if (_response.payloadId.has_value())
    {
        _result["payloadId"] = _response.payloadId.value();
    }
}
}  // namespace bcos::rpc

