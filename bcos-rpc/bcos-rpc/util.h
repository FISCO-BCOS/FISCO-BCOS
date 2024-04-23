/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file util.h
 * @author: jdkuang
 * @date 2024/4/24
 */

#pragma once
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/Common.h>

namespace bcos::rpc
{
std::tuple<protocol::BlockNumber, bool> getBlockNumberByTag(
    protocol::BlockNumber latest, std::string_view blockTag);
}  // namespace bcos::rpc