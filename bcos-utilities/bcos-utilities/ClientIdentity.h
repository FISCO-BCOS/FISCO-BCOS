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
 * @file ClientIdentity.h
 * @brief The one string this node identifies itself with to Ethereum-side peers.
 */
#pragma once

#include "include/BuildInfo.h"
#include <string>

namespace bcos
{
/// `fisco-bcos/v<version>/<os>/<compiler>` (ADR 0003): the devp2p Hello clientId on the
/// Ethereum and OP sync lanes and the `version` field of engine_getClientVersionV1 read
/// this one function, so the identity a CL sees over the Engine API and the identity a
/// peer sees in the RLPx handshake never drift apart. Built from the BuildInfo.h macros
/// (generated per build under ${PROJECT_BINARY_DIR}/include, on every target's include
/// path via BuildInfo.cmake).
inline std::string clientIdentity()
{
    return std::string("fisco-bcos/v") + FISCO_BCOS_PROJECT_VERSION + "/" + FISCO_BCOS_BUILD_OS +
           "/" + FISCO_BCOS_BUILD_COMPILER;
}
}  // namespace bcos
