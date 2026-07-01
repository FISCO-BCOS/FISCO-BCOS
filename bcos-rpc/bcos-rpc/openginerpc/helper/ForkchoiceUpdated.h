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
 * @file ForkchoiceUpdated.h
 * @date 2026/5/21
 */

#pragma once

#include "NewPayload.h"
#include "Helper.h"
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>

namespace bcos::rpc
{
inline std::optional<bcos::engine::PayloadAttributes> parsePayloadAttributes(
    Json::Value const& params, engine::ApiVersion version)
{
    return {};
}

inline bcos::engine::ForkchoiceState parseForkchoiceState(Json::Value const& params)
{
    return {};
}

inline Json::Value combineForkchoiceUpdatedResult(
    bcos::engine::ForkchoiceUpdatedResult const& result, engine::ApiVersion version)
{
    return {};
}
}  // namespace bcos::rpc
