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
#include "bcos-ops/RpcCall.h"
#include <iosfwd>

namespace bcos::ops
{
/// tx smoke|deploy|call|send|get|raw|sign on _connection; positionals start at the subcommand
int runTxOn(Connection const& _connection, Args const& _args, std::ostream& _out);
/// block get <height|hash> [--txs] [--proof]
int runBlockOn(Connection const& _connection, Args const& _args, std::ostream& _out);
/// txpool admission rejections (protocol::TransactionStatus, bcos-protocol/TransactionStatus.h)
/// reach the client as a JSON-RPC error whose code is the status value:
///   10000 NonceCheckFail       duplicate nonce (MemoryStorage.cpp verifyAndSubmit)
///   10001 BlockLimitCheckFail  blockLimit already passed or too far ahead
///   10002 TxPoolIsFull         txpool.limit reached
///   10023 MaxGasLimitExceeded  gasLimit above tx_gas_limit (TxValidator.cpp checkMaxGasLimit)
/// The tx commands print them as {"error":{"code","message","method"}} and exit 2.
constexpr int c_statusNonceCheckFail = 10000;
constexpr int c_statusBlockLimitCheckFail = 10001;
constexpr int c_statusTxPoolIsFull = 10002;
constexpr int c_statusMaxGasLimitExceeded = 10023;
}  // namespace bcos::ops
