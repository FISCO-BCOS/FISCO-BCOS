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
 * @file BlockResponse.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::rpc
{
/// Combine a block into the JSON response. @p canonicalHash, when given, is the SINGLE
/// authoritative block hash used for result["hash"] AND every full-tx entry's blockHash —
/// the endpoints resolve it once (ledger::getBlockHash, falling back to opAwareBlockHash)
/// so one response never mixes two derivations of the same block's hash. When nullopt the
/// OP-aware derivation is used (legacy callers / tests).
void combineBlockResponse(Json::Value& result, const bcos::protocol::Block& block,
    bool fullTxs = false, std::optional<bcos::crypto::HashType> canonicalHash = std::nullopt);

/// OP-aware block hash. OP headers (Isthmus+ always carry `withdrawalsRoot`; FISCO non-OP
/// headers never do) hash as keccak(encodeOpHeader) with the post-merge protocol constants — the
/// hash the OP block tables (`s_number_2_hash`) and op-node agree on — instead of the tars
/// `dataHash` fallback (which the read path re-derives as a FISCO tars hash). Shared by
/// `combineBlockResponse` and the endpoints' canonical-hash resolution so all read paths agree.
bcos::crypto::HashType opAwareBlockHash(const bcos::protocol::BlockHeader& header);
}  // namespace bcos::rpc