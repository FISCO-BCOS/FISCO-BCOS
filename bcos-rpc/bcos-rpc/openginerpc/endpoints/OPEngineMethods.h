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
 * @file OPEngineMethods.h
 * @date 2026/5/20
 */

#pragma once

#include <magic_enum/magic_enum.hpp>


namespace bcos::rpc
{
enum class OPEngineMethod : uint8_t
{
    engine_exchangeCapabilities,
    engine_forkchoiceUpdatedV1,
    engine_forkchoiceUpdatedV2,
    engine_forkchoiceUpdatedV3,
    engine_forkchoiceUpdatedV4,
    engine_getPayloadV1,
    engine_getPayloadV2,
    engine_getPayloadV3,
    engine_getPayloadV4,
    engine_newPayloadV1,
    engine_newPayloadV2,
    engine_newPayloadV3,
    engine_newPayloadV4,
};

inline std::string methodString(OPEngineMethod _method)
{
    return std::string(magic_enum::enum_name(_method));
}
}  // namespace bcos::rpc
