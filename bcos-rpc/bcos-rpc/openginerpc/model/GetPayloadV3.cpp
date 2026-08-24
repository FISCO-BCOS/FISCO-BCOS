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
 * @file GetPayloadV3.cpp
 * @date 2026/5/21
 */

#include "GetPayloadV3.h"

namespace bcos::rpc
{
std::tuple<bool, GetPayloadV3Request> decodeGetPayloadV3Request(Json::Value const& _params)
{
    GetPayloadV3Request request;
    if (!_params.isArray() || _params.size() != 1 || !_params[0].isString())
    {
        return {false, request};
    }

    request.payloadId = _params[0].asString();
    return {true, std::move(request)};
}

void combineGetPayloadV3Response(Json::Value& _result, GetPayloadV3Response const& _response)
{
    Json::Value executionPayload(Json::objectValue);
    appendExecutionPayloadV3(executionPayload, _response.executionPayload);
    _result["executionPayload"] = std::move(executionPayload);
    _result["blockValue"] = _response.blockValue;

    Json::Value blobsBundle(Json::objectValue);
    appendBlobsBundle(blobsBundle, _response.blobsBundle);
    _result["blobsBundle"] = std::move(blobsBundle);

    _result["shouldOverrideBuilder"] = _response.shouldOverrideBuilder;
}
}  // namespace bcos::rpc

