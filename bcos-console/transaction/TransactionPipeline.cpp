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
 * @brief: TransactionPipeline implementation
 * @file: TransactionPipeline.cpp
 */

#include "TransactionPipeline.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <condition_variable>
#include <iostream>
#include <mutex>

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

int64_t TransactionPipeline::fetchBlockNumber()
{
    std::mutex mtx;
    std::condition_variable cv;
    int64_t result = -1;
    bool done = false;

    m_connection->getBlockNumber(
        m_groupID, m_connection->defaultNodeName(), [&](bcos::Error::Ptr error, Json::Value& r) {
            std::lock_guard<std::mutex> lock(mtx);
            if (!error)
            {
                result = r.asInt64();
            }
            done = true;
            cv.notify_one();
        });

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return done; });
    return result;
}

void TransactionPipeline::send(std::string_view contractAddr, const bcos::bytes& data,
    std::string_view abi, TxResultCallback callback)
{
    // Validate we have a key for signing
    auto keyPair = m_keyManager->currentKeyPair();
    if (!keyPair)
    {
        callback(false, "No account loaded for signing. Use loadAccount <path> first.");
        return;
    }

    // Get current block number for blockLimit
    int64_t blockNumber = fetchBlockNumber();
    if (blockNumber < 0)
    {
        callback(false, "Failed to fetch block number from chain.");
        return;
    }
    int64_t blockLimit = blockNumber + BLOCK_LIMIT_OFFSET;

    // Build the signed transaction
    std::pair<std::string, std::string> signedTx;
    try
    {
        signedTx = m_txBuilder->createSignedTransaction(*keyPair,  // signing key pair
            m_groupID,                                             // group ID
            m_chainID,                                             // chain ID
            std::string(contractAddr),                             // "to" address
            data,                                                  // ABI-encoded input data (bytes)
            std::string(abi),                                      // ABI JSON string
            blockLimit,                                            // block limit
            0,                                                     // attribute (0 = normal)
            ""                                                     // extraData
        );
    }
    catch (std::exception const& e)
    {
        callback(false, std::string("Transaction build error: ") + e.what());
        return;
    }

    // signedTx.first  = tx hash (hex, no prefix)
    // signedTx.second = signed transaction bytes (hex, no prefix)
    auto txHash = signedTx.first;
    auto txHex = signedTx.second;

    std::cout << "[DEBUG] Signed transaction hash: 0x" << txHash << '\n';

    // Send via JSON-RPC
    m_connection->sendTransaction(m_groupID, m_connection->defaultNodeName(), txHex,
        false,  // requireProof
        [callback = std::move(callback), txHash](bcos::Error::Ptr error, Json::Value& result) {
            if (error)
            {
                callback(false, "Send error: " + error->errorMessage());
                return;
            }
            callback(true, txHash);
        });
}

std::pair<bool, std::string> TransactionPipeline::sendSync(
    std::string_view contractAddr, const bcos::bytes& data, std::string_view abi)
{
    std::mutex mtx;
    std::condition_variable cv;
    bool success = false;
    std::string result;
    bool done = false;

    send(contractAddr, data, abi, [&](bool ok, std::string msg) {
        std::lock_guard<std::mutex> lock(mtx);
        success = ok;
        result = std::move(msg);
        done = true;
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return done; });
    return {success, result};
}

}  // namespace bcos::console
