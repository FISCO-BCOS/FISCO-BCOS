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
 * @file SystemTransaction.h
 * @brief Which transactions count as system transactions, in one place.
 *
 * Deliberately NOT in txpool/utilities/Common.h: that header is included by nearly every
 * translation unit in this module, and the precompiled headers this needs bring names that
 * collide with the consensus ones several of those TUs already have in scope.
 */
#pragma once
// trimHexPrefix is header-only and lives here; PrecompiledTypeDef supplies c_systemTxsAddress
// and contains().
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/protocol/Transaction.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <string>
#include <string_view>

namespace bcos::txpool
{
/// Whether @p tx targets one of the system contracts, which is what earns it setSystemTx(true).
///
/// A BCOS transaction's `to` is already stored the way c_systemTxsAddress spells it. A Web3
/// transaction's is not: it arrives 0x-prefixed and in whatever case the sender used, so it is
/// trimmed and lowered first. Getting that wrong in one direction lets a system call through as
/// an ordinary transaction; in the other it grants system standing to an address that merely
/// differs in case.
inline bool isSystemTransaction(protocol::Transaction const& tx)
{
    if (tx.type() == static_cast<uint8_t>(protocol::TransactionType::BCOSTransaction)) [[likely]]
    {
        return precompiled::contains(bcos::precompiled::c_systemTxsAddress, tx.to());
    }
    auto to = precompiled::trimHexPrefix(tx.to());
    if (to.empty()) [[unlikely]]
    {
        return false;  // deployment
    }
    std::string lower{to};
    boost::algorithm::to_lower(lower);
    return precompiled::contains(bcos::precompiled::c_systemTxsAddress, std::string_view{lower});
}

// Trigger a transaction cleanup operation every 3s
}  // namespace bcos::txpool
