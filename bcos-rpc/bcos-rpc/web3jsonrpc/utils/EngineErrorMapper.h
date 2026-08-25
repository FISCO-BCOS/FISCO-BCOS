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
 * @file EngineErrorMapper.h
 * @brief Map engine-service exceptions to the execution-apis JSON-RPC error codes
 */

#pragma once

#include "bcos-rpc/web3jsonrpc/utils/Common.h"  // EngineError
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/Types.h>  // UnsupportedEngineApiVersion / UnknownPayload / IncompatiblePayloadVersion
#include <bcos-rpc/jsonrpc/Common.h>  // JsonRpcError::InternalError
#include <bcos-utilities/Exceptions.h>

namespace bcos::rpc
{
/// Map engine-service exceptions to the execution-apis JSON-RPC error codes the Engine API
/// assigns (specs.optimism.io exec-engine.md references the execution-apis error table).
/// Unmapped conditions stay -32603 InternalError: a mapped code must be unambiguous.
/// `bcos::Error` (storage/service faults) intentionally maps to InternalError.
inline int32_t mapEngineErrorCode(bcos::Exception const& e) noexcept
{
    if (dynamic_cast<bcos::engine::UnsupportedFork const*>(&e) ||
        dynamic_cast<bcos::engine::IncompatiblePayloadVersion const*>(&e) ||
        dynamic_cast<bcos::engine::UnsupportedEngineApiVersion const*>(&e))
    {
        return EngineError::UnsupportedFork;  // -38005
    }
    if (dynamic_cast<bcos::engine::UnknownPayload const*>(&e))
    {
        return EngineError::UnknownPayload;  // -38001
    }
    if (dynamic_cast<bcos::engine::InvalidForkchoiceState const*>(&e))
    {
        return EngineError::InvalidForkchoiceState;  // -38002
    }
    if (dynamic_cast<bcos::engine::UnsupportedOpPayloadAttributes const*>(&e))
    {
        return EngineError::InvalidPayloadAttributes;  // -38003
    }
    return InternalError;  // -32603
}
}  // namespace bcos::rpc
