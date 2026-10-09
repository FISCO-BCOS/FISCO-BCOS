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
 * @brief admin_* methods: registered only on the local socket, never on HTTP/WebSocket
 * @file AdminMethods.h
 */
#pragma once

#include "JsonRpcInterface.h"

namespace bcos::rpc
{
/// admin_getLogLevel() → {"global": "<level>", "modules": {"<CHANNEL>": "<level>"}}
/// admin_setLogLevel(level[, module]): no module → global; "inherit" resets the module
void registerAdminMethods(JsonRpcInterface& _rpc);
}  // namespace bcos::rpc
