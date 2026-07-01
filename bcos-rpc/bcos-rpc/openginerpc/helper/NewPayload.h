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
 * @file NewPayload.h
 * @date 2026/5/21
 */

#pragma once

#include <bcos-rpc/openginerpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include "Helper.h"


namespace bcos::rpc
{
inline bcos::engine::NewPayloadRequest parseNewPayloadRequest(
    Json::Value const& params, bcos::protocol::TransactionFactory& transactionFactory,
    engine::ApiVersion version)
{
    return {};
}

inline Json::Value serializePayloadStatus(
    bcos::engine::PayloadStatus const& status, engine::ApiVersion version)
{
    return {};
}

inline void combineNewPayloadResponse(
    Json::Value& _result, bcos::engine::PayloadStatus const& _response, engine::ApiVersion version)
{
}
}  // namespace bcos::rpc
