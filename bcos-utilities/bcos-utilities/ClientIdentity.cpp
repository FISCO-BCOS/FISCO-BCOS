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
 * @file ClientIdentity.cpp
 * @brief Builds the client identity string from the generated BuildInfo.h macros.
 */
#include "ClientIdentity.h"
#include "include/BuildInfo.h"

std::string bcos::clientIdentity()
{
    return std::string("fisco-bcos/v") + FISCO_BCOS_PROJECT_VERSION + "/" + FISCO_BCOS_BUILD_OS +
           "/" + FISCO_BCOS_BUILD_COMPILER;
}
