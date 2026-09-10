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
 * @file Constants.h
 * @brief Cross-layer consensus header constants shared by the engine builders and the
 * OP block seal (both stamp keccak256(rlp(header))-critical values).
 */

#pragma once

#include <string_view>

namespace bcos::engine
{
/// sha256("") — the EL "empty requests hash" (EIP-7685: a block with no execution
/// requests). Single-sourced here because two layers stamp it: the engine header builders
/// (EngineServiceCommon.h c_emptyRequestsHash, as bcos::h256) and the OP block seal
/// (OpBlockExecute.h OP_EMPTY_REQUESTS_HASH, as evmc::bytes32). The two layers cannot see
/// each other's headers (engine sits above opstack-executor), so the hex lives in the
/// framework and each layer casts it into its native type.
inline constexpr std::string_view c_emptyRequestsHashHex =
    "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
}  // namespace bcos::engine
