/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief host context
 * @file EVMHostInterface.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../Common.h"
#include "bcos-framework//protocol/BlockHeader.h"
#include "evmc/evmc.h"
#include "evmc/instructions.h"
#include <bcos-framework//protocol/LogEntry.h>
#include <boost/optional.hpp>
#include <functional>
#include <set>

namespace bcos
{
namespace executor
{
const evmc_host_interface* getHostInterface();
const wasm_host_interface* getWasmHostInterface();
}  // namespace executor
}  // namespace bcos
