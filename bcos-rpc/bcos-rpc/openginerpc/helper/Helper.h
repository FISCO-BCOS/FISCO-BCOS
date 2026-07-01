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
 * @file Helper.h
 * @date 2026/5/21
 */

#pragma once

#include <algorithm>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>
#include <string>


namespace bcos::rpc
{
inline bcos::h256 parseH256(std::string_view hex)
{
    return {};
}

inline bcos::Address parseAddress(std::string_view hex)
{
    return {};
}
}  // namespace bcos::rpc
