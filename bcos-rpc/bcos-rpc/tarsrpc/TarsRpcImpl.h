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
 * @brief impl for TarsRpcInterface
 * @file TarsRpcImpl.h
 * @author: octopus
 * @date 2023-02-22
 */

#pragma once
#include "bcos-rpc/tarsrpc/TarsRpcInterface.h"
#include "bcos-utilities/Common.h"
#include <bcos-boostssl/websocket/WsService.h>

namespace bcos::rpc
{

class TarsRpcImpl : public TarsRpcInterface, public std::enable_shared_from_this<TarsRpcImpl>
{
public:
    using Ptr = std::shared_ptr<TarsRpcImpl>;
    using ConstPtr = std::shared_ptr<const TarsRpcImpl>;

    TarsRpcImpl(std::shared_ptr<bcos::boostssl::ws::WsService> _wsService);
    TarsRpcImpl(const TarsRpcImpl&) = delete;
    TarsRpcImpl(TarsRpcImpl&&) = delete;
    TarsRpcImpl& operator=(const TarsRpcImpl&) = delete;
    TarsRpcImpl& operator=(TarsRpcImpl&&) = delete;
    ~TarsRpcImpl() override = default;

    void handleRequest(std::shared_ptr<boostssl::MessageFace> _message,
        std::shared_ptr<boostssl::ws::WsSession> _session) override;

    // send transaction
    void sendTransaction(const std::string& _groupID, const std::string& _nodeName,
        bcos::bytes&& _transactionData, SendTxCallback _respCallback) override;
    // response
    void onResponse(std::weak_ptr<boostssl::ws::WsSession> _session,
        std::shared_ptr<boostssl::MessageFace> _message, bcos::Error::Ptr _error,
        bcos::protocol::TransactionReceipt::Ptr _transactionReceipt);

public:
    std::shared_ptr<bcos::boostssl::ws::WsService> service() const { return m_wsService; }
    void setService(std::shared_ptr<bcos::boostssl::ws::WsService> _wsService)
    {
        m_wsService = std::move(_wsService);
    }

    GroupManager::Ptr groupManager() const { return m_groupManager; }
    void setGroupManager(GroupManager::Ptr _groupManager)
    {
        m_groupManager = std::move(_groupManager);
    }

private:
    std::shared_ptr<bcos::boostssl::ws::WsService> m_wsService;
    GroupManager::Ptr m_groupManager;
};

}  // namespace bcos::rpc
