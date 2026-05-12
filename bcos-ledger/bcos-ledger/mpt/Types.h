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
 * @file Types.h
 * @brief Type aliases for the MPT module (spec §5.5)
 */
#pragma once

#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger::mpt
{

/// 32-byte hash type used throughout the MPT (= keccak256 digest size)
using Hash32 = bcos::h256;

/// 20-byte Ethereum account address
using Address20 = bcos::h160;

}  // namespace bcos::ledger::mpt
