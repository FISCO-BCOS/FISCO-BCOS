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
 * @file HistoryDepths.h
 * @brief The two retention depths, as one value (spec §10.3, §12)
 */
#pragma once

#include <bcos-framework/protocol/ProtocolTypeDef.h>

namespace bcos::ledger::mpt::history
{

/// How many blocks back each reverse history is kept.
///
/// These are NODE-LOCAL OPERATIONS parameters, not consensus values: they change what this node
/// can ANSWER, never what it computes or agrees on. Two nodes of the same chain may run different
/// depths, and one may run zero. That is why they arrive through nodeConfig's `[storage]` section
/// (`mpt_history_state_blocks` / `mpt_history_proof_blocks`) rather than through sys-config or
/// Features.
///
/// Zero means DISABLED for that history: the commit path writes none of its rows and every query
/// that would need them is refused with an explicit "not retained on this node" error rather than
/// being answered from the current state (G6). The two are independent — spec §10.3 recommends
/// starting with `state == proof` and splitting later (an archive node raises `state`, a
/// proof-serving node raises `proof`), and §10.2 says disabling `proof` leaves historical STATE
/// working.
///
/// The default here is DISABLED on purpose: the only production source of a non-zero depth is
/// NodeConfig (whose own default is 128), so nothing silently starts writing history because a
/// wiring site forgot to inject.
struct HistoryDepths
{
    /// H_state — historical flat-state reads (callAtBlock, spec §10.1).
    protocol::BlockNumber state{0};
    /// H_proof — historical trie-node versions (eth_getProof, spec §10.2).
    protocol::BlockNumber proof{0};

    [[nodiscard]] bool anyEnabled() const noexcept { return state > 0 || proof > 0; }

    friend bool operator==(HistoryDepths const&, HistoryDepths const&) noexcept = default;
};

}  // namespace bcos::ledger::mpt::history
