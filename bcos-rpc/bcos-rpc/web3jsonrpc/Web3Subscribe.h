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
 * @file Web3Subscribe.h
 * @author: octopus
 * @date 2025/08/06
 */

#pragma once
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <json/json.h>
#include <unordered_map>
#include <unordered_set>

namespace bcos::rpc
{

class Web3JsonRpcImpl;
using Web3JsonRpcImplWeakPtr = std::weak_ptr<Web3JsonRpcImpl>;

/**
 * @brief Web3Subscribe is a class that handles Web3 subscriptions.
 */
class Web3Subscribe : public std::enable_shared_from_this<Web3Subscribe>
{
public:
    using Ptr = std::shared_ptr<Web3Subscribe>;

    Web3Subscribe(std::weak_ptr<Web3JsonRpcImpl> _web3JsonRpcImpl)
      : m_web3JsonRpcImpl(std::move(_web3JsonRpcImpl))
    {
        WEB3_LOG(INFO) << LOG_KV("[NEWOBJ][Web3Subscribe]", this);
    }
    Web3Subscribe(const Web3Subscribe&) = delete;
    Web3Subscribe& operator=(const Web3Subscribe&) = delete;
    Web3Subscribe(Web3Subscribe&&) = delete;
    Web3Subscribe& operator=(Web3Subscribe&&) = delete;
    ~Web3Subscribe() { WEB3_LOG(INFO) << LOG_KV("[DELOBJ][Web3Subscribe]", this); }

    /**
     * @brief onSubscribeRequest
     */
    std::string onSubscribeRequest(
        const Json::Value& request, std::shared_ptr<bcos::boostssl::ws::WsSession> session);
    /**
     * @brief onUnsubscribeRequest
     */
    bool onUnsubscribeRequest(
        const Json::Value& request, std::shared_ptr<bcos::boostssl::ws::WsSession> session);

    /**
     * @brief onRemoveSubscribeBySession
     */
    void onRemoveSubscribeBySession(std::shared_ptr<bcos::boostssl::ws::WsSession> session);

    /**
     * @brief onSubscribeNewHeads
     */
    std::string onSubscribeNewHeads(
        const Json::Value& request, std::shared_ptr<bcos::boostssl::ws::WsSession> session);

    /**
     * @brief eraseSession
     */
    void eraseSession(std::shared_ptr<bcos::boostssl::ws::WsSession> session);

    /**
     * @brief generateSubscriptionId
     */
    std::string generateSubscriptionId();

    /**
     * @brief onNewBlock
     */
    void onNewBlock(int64_t blockNumber);

private:
    std::mutex m_mutex;

    // WsSession endpoint -> subscription id
    std::unordered_map<std::string, std::unordered_set<std::string>> m_endpoint2SubscriptionIds;


    // newHeads -> WsSession
    std::unordered_map<std::string, std::shared_ptr<bcos::boostssl::ws::WsSession>>
        m_newHeads2Session;

    // pendingTransactions -> WsSession
    // std::unordered_map<std::string, std::shared_ptr<bcos::boostssl::ws::WsSession>>
    //     m_pendingTransactions2Session;

    // logs -> WsSession
    // std::unordered_map<std::string, std::shared_ptr<bcos::boostssl::ws::WsSession>>
    // m_logs2Session;

    std::weak_ptr<Web3JsonRpcImpl> m_web3JsonRpcImpl;
};
}  // namespace bcos::rpc
