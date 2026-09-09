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
 * @file AdmissionError.h
 * @author: kyonGuo
 * @date 2026/9/9
 */

#pragma once
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <string_view>

namespace bcos::rpc
{
/// The JSON-RPC error a refused eth_sendRawTransaction answers with -- one table for both pools,
/// so the txpool branch and the mempool branch refuse the same transaction with the same code and
/// the same words.
///
/// The split is the one #5555's review recorded. -32602 (InvalidParams) is for a parameter that
/// is not a transaction this node can accept as-is: bytes that decode but carry no valid
/// signature, or a chain id that is not this chain's. -32000 is for a well-formed transaction a
/// protocol or pool rule turned away. (geth answers -32000 for all of these and keeps -32602 for
/// arguments its RPC layer cannot decode.) Where geth has words for the refusal the message is
/// geth's, because clients match on them: viem classifies "already known", "insufficient funds",
/// "intrinsic gas too low", "nonce has max value" and the two fee-cap sentences by regex, ethers
/// "insufficient funds"; either then shows the user something better than a status name. A status
/// geth has no words for, or whose one name covers several of geth's sentences, keeps its own
/// name. Unknown -- admission could not read what it needed -- is the node's fault, not the
/// transaction's, and answers -32603 with a fixed sentence; the storage diagnostic is for the log.
JsonRpcException admissionError(protocol::TransactionStatus status);

/// The same, with a detail appended: "transaction type not supported (blob)". The detail follows
/// geth's words so a client matching on them still does.
JsonRpcException admissionError(protocol::TransactionStatus status, std::string_view detail);
}  // namespace bcos::rpc
