/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief: TransactionPipeline — builds, signs, and sends precompiled governance
 *         transactions using bcos-sdk TransactionBuilder.
 * @file: TransactionPipeline.h
 */

#pragma once

#include "../connection/RpcConnection.h"
#include "../keymanager/KeyManager.h"
#include "bcos-cpp-sdk/utilities/tx/TransactionBuilder.h"
#include <bcos-task/Task.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::console
{

/**
 * @brief Pipeline that takes ABI-encoded precompiled call data, signs it,
 *        and sends the transaction to the chain.
 *
 * The typical call site uses task::syncWait to block on the coroutine:
 *   auto [ok, result] = task::syncWait(pipeline.send(addr, data, abi));
 *
 * Internally the coroutine suspends on the block-number query and the
 * send-transaction RPC via the Awaitable pattern (see LedgerMethods).
 */
class TransactionPipeline
{
public:
    using Ptr = std::shared_ptr<TransactionPipeline>;

    TransactionPipeline(RpcConnection::Ptr connection, KeyManager::Ptr keyManager,
        std::string groupID, std::string chainID = "chain0");

    ~TransactionPipeline() = default;

    /**
     * @brief Coroutine: build, sign, and send a precompiled transaction.
     *
     * Suspends while fetching the block number and while the send RPC is in
     * flight.  Callers that are not themselves in a coroutine can block with
     * bcos::task::syncWait.
     *
     * @return pair<success, txHash_or_errorMessage>
     */
    task::Task<std::pair<bool, std::string>> send(
        std::string_view contractAddr, const bcos::bytes& data, std::string_view abi);

    /// Accessors
    const std::string& groupID() const { return m_groupID; }
    const std::string& chainID() const { return m_chainID; }
    void setChainID(std::string id) { m_chainID = std::move(id); }

    /// Default block limit offset (added to current block number)
    static constexpr int64_t BLOCK_LIMIT_OFFSET = 500;

private:
    RpcConnection::Ptr m_connection;
    KeyManager::Ptr m_keyManager;
    std::string m_groupID;
    std::string m_chainID;
    std::unique_ptr<bcos::cppsdk::utilities::TransactionBuilder> m_txBuilder;
};

}  // namespace bcos::console
