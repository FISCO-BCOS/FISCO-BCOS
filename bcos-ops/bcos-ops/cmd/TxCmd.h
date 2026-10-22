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
 * @brief entry points of the tx and block commands on an existing connection (tests inject a
 *        FakeRpc here; the registered commands connect first and call these)
 * @file TxCmd.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include <iosfwd>

namespace bcos::ops
{
/// tx smoke|deploy|call|send|get|raw|sign on _connection; positionals start at the subcommand
int runTxOn(Connection const& _connection, Args const& _args, std::ostream& _out);
/// block get <height|hash> [--txs] [--proof]
int runBlockOn(Connection const& _connection, Args const& _args, std::ostream& _out);
/// prints a JSON-RPC refusal as {"error":{"code","message","method"}} (text: `rejected: …`)
/// and returns exit code 2 (the chain answered; the request was not admitted)
int printRpcRefusal(std::ostream& _out, OpsRpcError const& _error, bool _json);
/// A JSON-RPC error (OpsRpcError) raised by any tx or block subcommand is printed once by
/// runTxOn / runBlockOn as {"error":{"code","message","method"}} (text mode: `rejected: …`)
/// with exit 2. For a txpool admission rejection the code is the protocol::TransactionStatus
/// value (bcos-protocol/bcos-protocol/TransactionStatus.h; MemoryStorage.cpp verifyAndSubmit):
///   10000 NonceCheckFail       duplicate nonce
///   10001 BlockLimitCheckFail  blockLimit already passed or too far ahead
///   10002 TxPoolIsFull         txpool.limit reached
/// A gasLimit above tx_gas_limit is not an admission check for tars transactions
/// (bcos-tx-validator CheckSet.h: the Bcos column carries no MaxGasLimit); it surfaces at
/// execution as receipt status 2 OutOfGasLimit, so no 10023 ever reaches this client.
}  // namespace bcos::ops
