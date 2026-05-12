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
 * @file Constants.cpp
 * @brief Definitions of Ethereum well-known hash constants (Meyers singletons)
 */

#include "Constants.h"
#include <string>

namespace bcos::ledger::mpt
{

Hash32 const& emptyRootHash()
{
    static Hash32 const s_value{EMPTY_ROOT_HASH_HEX, Hash32::FromHex};
    return s_value;
}

Hash32 const& emptyCodeHash()
{
    static Hash32 const s_value{EMPTY_CODE_HASH_HEX, Hash32::FromHex};
    return s_value;
}

}  // namespace bcos::ledger::mpt
