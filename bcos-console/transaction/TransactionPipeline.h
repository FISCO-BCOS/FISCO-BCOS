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
#include "../precompiled/PrecompiledContractInfo.h"

#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace bcos::console
{

/// Callback for transaction result: (success, txHash or error message)
using TxResultCallback = std::function<void(bool success, std::string txHashOrError)>;

/**
 * @brief Pipeline that takes ABI-encoded precompiled call data, signs it,
 *        and sends the transaction to the chain.
 *
 * The typical flow:
 *   1. User command → PrecompiledContract::encode() → bytes
 *   2. pipeline.send(contractAddr, encodedData, abi, callback)
 *   3. Internally: getBlockNumber() → blockLimit=blockNumber+500
 *      → TransactionBuilder::createSignedTransaction()
 *      → RpcConnection::sendTransaction()
 */
class TransactionPipeline
{
public:
    using Ptr = std::shared_ptr<TransactionPipeline>;

    TransactionPipeline(RpcConnection::Ptr connection, KeyManager::Ptr keyManager,
        std::string groupID, std::string chainID = "chain0");

    ~TransactionPipeline() = default;

    /**
     * @brief Build, sign, and send a precompiled transaction asynchronously.
     *
     * @param contractAddr  Precompiled contract address (no 0x prefix, 40 hex)
     * @param data          ABI-encoded call data (returned by PrecompiledContract::encode)
     * @param abi           ABI JSON string for the precompiled contract
     * @param callback      Called when the send completes: (success, hash|error)
     */
    void send(std::string_view contractAddr, const bcos::bytes& data, std::string_view abi,
        TxResultCallback callback);

    /**
     * @brief Synchronous version: blocks until the send completes.
     * @return pair<success, hash_or_error>
     */
    std::pair<bool, std::string> sendSync(
        std::string_view contractAddr, const bcos::bytes& data, std::string_view abi);

    /**
     * @brief Synchronous helper: query current block number.
     *
     * Used internally to compute blockLimit.
     * Blocks until the async call returns.
     */
    int64_t fetchBlockNumber();

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
