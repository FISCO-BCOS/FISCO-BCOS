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
 * @file Common.h
 * @date 2026/5/20
 */

#pragma once

#include <bcos-rpc/jsonrpc/Common.h>

#define OP_ENGINE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[RPC][OPENGINE]"

namespace bcos::rpc
{
enum EngineError : int32_t
{
    // -38000: Engine API base
    UnknownPayload = -38001,
    InvalidForkchoiceState = -38002,
    InvalidPayloadAttributes = -38003,
    TooLargeRequest = -38004,
    UnsupportedFork = -38005,
    TooDeepReorg = -38006,
};
}  // namespace bcos::rpc
