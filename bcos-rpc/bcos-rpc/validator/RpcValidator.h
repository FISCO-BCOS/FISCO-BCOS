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
 * @file RpcValidator.h
 * @author: asherli
 * @date 2024/12/12
 */
#pragma once

#include "bcos-protocol/TransactionStatus.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <json/json.h>
#include <string_view>

namespace bcos::rpc
{
// template <typename RpcTransactionTemplate>
//     requires(std::same_as<RpcTransactionTemplate, protocol::Transaction::Ptr> ||
//              std::same_as<RpcTransactionTemplate, Web3Transaction>)
class RpcValidator
{
public:
    // EIP-155: Simple replay attack protection
    // static bool checkWeb3ReplayedProtected(
    //     std::optional<uint64_t> _txChainId, std::string _chainId);
    // static bool checkBcosReplayedProtected(
    //     protocol::Transaction::Ptr _tx, std::string_view _chainId);

    // EIP-2681: Limit account nonce to 2^64-1
    // EIP-3860: Limit and meter initcode
    // EIP-3607: Reject transactions from senders with deployed code
    // Enough balance
    static task::Task<protocol::TransactionStatus> checkTransactionValidator(
        protocol::Transaction::Ptr _tx, ledger::LedgerInterface::Ptr _ledger);
    static void handleTransactionStatus(protocol::TransactionStatus transactionStatus);
};

}  // namespace bcos::rpc