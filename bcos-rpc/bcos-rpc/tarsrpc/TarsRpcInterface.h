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
 * @brief interface for TarsRPC
 * @file TarsRpcInterface.h
 * @author: octopus
 * @date 2023-02-22
 */

#pragma once

#include "bcos-boostssl/websocket/WsSession.h"
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "bcos-rpc/groupmgr/NodeService.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-utilities/Error.h>

namespace bcos::rpc
{

class TarsRpcInterface
{
public:
    using Ptr = std::shared_ptr<TarsRpcInterface>;
    using ConstPtr = std::shared_ptr<const TarsRpcInterface>;

    TarsRpcInterface() = default;
    TarsRpcInterface(const TarsRpcInterface&) = delete;
    TarsRpcInterface(TarsRpcInterface&&) = delete;
    TarsRpcInterface& operator=(const TarsRpcInterface&) = delete;
    TarsRpcInterface& operator=(TarsRpcInterface&&) = delete;
    virtual ~TarsRpcInterface() = default;

    virtual void handleRequest(std::shared_ptr<boostssl::MessageFace> _message,
        std::shared_ptr<boostssl::ws::WsSession> _session) = 0;

    using SendTxCallback =
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)>;
    virtual void sendTransaction(const std::string& _groupID, const std::string& _nodeName,
        bcos::bytes&& _transactionData, SendTxCallback _respCallback) = 0;
};

}  // namespace bcos::rpc