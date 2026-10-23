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
 * @brief the RPC addition to `log tx`: is the hash on chain? Separates "node unreachable" from
 *        "reachable but the receipt is not there" so the operator does not misread the second as
 *        the first.
 * @file TxLookup.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <json/json.h>
#include <string>

namespace bcos::ops
{
struct TxLookup
{
    enum class Kind
    {
        Found,       // receipt with a block number
        NotOnChain,  // the node answered, but no receipt for this hash
        Unavailable  // no node answered (connect failure)
    };
    Kind kind = Kind::Unavailable;
    std::string source;  // rpc | attach when a node answered
    int64_t blockNumber = -1;
    int status = -1;
    std::string detail;  // the error text for NotOnChain / Unavailable
    Json::Value toJson() const;
    std::string line() const;  // one terminal line, e.g. "rpc: included in block 129, status 0"
    /// a connect failure: no node answered, the hash may well be on chain
    static TxLookup unavailable(std::string _detail);
};

/// classifies a connected node's getTransactionReceipt answer; never throws
TxLookup lookupTxOnChain(RpcCall const& _call, std::string const& _source,
    std::string const& _group, std::string const& _hash);
}  // namespace bcos::ops
