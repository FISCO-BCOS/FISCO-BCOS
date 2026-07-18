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
 * @brief: TransactionPipeline — coroutine-based send with Awaitable bridges
 * @file: TransactionPipeline.cpp
 */

#include "TransactionPipeline.h"
#include <bcos-utilities/Error.h>
#include <coroutine>
#include <iostream>

namespace bcos::console
{

TransactionPipeline::TransactionPipeline(RpcConnection::Ptr connection, KeyManager::Ptr keyManager,
    std::string groupID, std::string chainID)
  : m_connection(std::move(connection)),
    m_keyManager(std::move(keyManager)),
    m_groupID(std::move(groupID)),
    m_chainID(std::move(chainID))
{
    m_txBuilder = std::make_unique<bcos::cppsdk::utilities::TransactionBuilder>();
}

// ---- Awaitable bridges (analogous to LedgerMethods pattern) ----

namespace
{

/// Suspend until getBlockNumber completes; resume stores the value.
struct AwaitBlockNumber
{
    RpcConnection& conn;
    std::string_view groupID;
    std::string_view nodeName;
    int64_t result = -1;
    bcos::Error::Ptr error;

    static constexpr bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle)
    {
        conn.getBlockNumber(
            groupID, nodeName, [this, handle](bcos::Error::Ptr err, Json::Value& r) {
                if (err)
                    error = std::move(err);
                else
                    result = r.asInt64();
                handle.resume();
            });
    }
    int64_t await_resume() const { return result; }
};

/// Suspend until sendTransaction completes; error (if any) stored in member.
struct AwaitSendTx
{
    RpcConnection& conn;
    std::string_view groupID;
    std::string_view nodeName;
    std::string txHex;
    bool requireProof = false;
    bcos::Error::Ptr error;

    static constexpr bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle)
    {
        conn.sendTransaction(groupID, nodeName, txHex, requireProof,
            [this, handle](bcos::Error::Ptr err, Json::Value& /*result*/) {
                error = std::move(err);
                handle.resume();
            });
    }
    void await_resume() const noexcept {}
};

}  // anonymous namespace

// ---- Public API ----

task::Task<std::pair<bool, std::string>> TransactionPipeline::send(
    std::string_view contractAddr, const bcos::bytes& data, std::string_view abi)
{
    // Validate key
    auto keyPair = m_keyManager->currentKeyPair();
    if (!keyPair)
    {
        co_return std::pair{
            false, std::string("No account loaded for signing. Use loadAccount <path> first.")};
    }

    // Fetch block number asynchronously
    auto blockNumber = co_await AwaitBlockNumber{
        .conn = *m_connection,
        .groupID = m_groupID,
        .nodeName = m_connection->defaultNodeName(),
        .result = -1,
        .error = nullptr,
    };
    if (blockNumber < 0)
    {
        co_return std::pair{false, std::string("Failed to fetch block number from chain.")};
    }
    int64_t blockLimit = blockNumber + BLOCK_LIMIT_OFFSET;

    // Build signed transaction
    std::pair<std::string, std::string> signedTx;
    try
    {
        signedTx = m_txBuilder->createSignedTransaction(*keyPair, m_groupID, m_chainID,
            std::string(contractAddr), data, std::string(abi), blockLimit, 0, "");
    }
    catch (std::exception const& e)
    {
        co_return std::pair{false, std::string("Transaction build error: ") + e.what()};
    }

    auto txHash = signedTx.first;
    auto txHex = signedTx.second;
    auto nodeName = m_connection->defaultNodeName();

    std::cout << "[DEBUG] Signed transaction hash: 0x" << txHash << '\n';

    // Send transaction asynchronously
    auto awaitSend = AwaitSendTx{
        .conn = *m_connection,
        .groupID = m_groupID,
        .nodeName = nodeName,
        .txHex = txHex,
        .requireProof = false,
        .error = nullptr,
    };
    co_await awaitSend;

    if (awaitSend.error)
    {
        co_return std::pair{false, "Send error: " + awaitSend.error->errorMessage()};
    }

    co_return std::pair{true, std::move(txHash)};
}

}  // namespace bcos::console
