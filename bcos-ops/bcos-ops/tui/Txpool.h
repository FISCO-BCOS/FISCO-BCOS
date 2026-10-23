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
 * @brief page 2 in the EL modes (spec §5.2.4): the transaction pool. opstack-el with the web3
 *        listener on reads txpool_content (EthEndpoint.cpp, OP lane only) and flattens
 *        pending / queued × sender × nonce into rows; el mode or a disabled listener shows only
 *        the getPendingTxSize count with the reason in the title
 * @file Txpool.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <json/json.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
struct TxpoolRow
{
    bool pending = true;  // false: queued (nonce gap)
    std::string hash;
    std::string from;
    int64_t nonce = -1;
    std::string to;
    std::string value;  // as the node returns it (0x quantity)
};

struct TxpoolState
{
    bool content = false;  // txpool_content was read; otherwise only the count is known
    std::string note;      // why the content is not shown ("mode=el", "web3_rpc disabled")
    std::vector<TxpoolRow> rows;
    std::string error;  // last txpool_content failure
};

/// pending first, then queued; within each by sender, then nonce
std::vector<TxpoolRow> flattenTxpoolContent(Json::Value const& _content);
/// _web3 is null when the listener is off; _mode is the connection's EL mode
TxpoolState txpoolFetch(std::string const& _mode, bcos::ops::RpcCall const* _web3,
    std::atomic<bool> const* _stop = nullptr);
}  // namespace bcos::ops::tui
