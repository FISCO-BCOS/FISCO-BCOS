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

#include <string>

namespace bcos
{
/// `fisco-bcos/v<version>/<os>/<compiler>` (ADR 0003): the devp2p Hello clientId on the
/// Ethereum and OP sync lanes and the `version` field of engine_getClientVersionV1 read
/// this one function, so the identity a CL sees over the Engine API and the identity a
/// peer sees in the RLPx handshake never drift apart. Defined in ClientIdentity.cpp from
/// the per-build generated version/os/compiler macros, which are never installed; this
/// header stays free of them so installed-package consumers can include it.
std::string clientIdentity();
}  // namespace bcos
