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
 * @brief interfaces for transaction sync
 * @file TransactionSyncInterface.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once
#include "bcos-txpool/sync/TransactionSyncConfig.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/protocol/Block.h>

namespace bcos
{
namespace sync
{
class TransactionSyncInterface
{
public:
    using Ptr = std::shared_ptr<TransactionSyncInterface>;
    explicit TransactionSyncInterface(TransactionSyncConfig::Ptr _config) : m_config(_config) {}

    virtual ~TransactionSyncInterface() {}

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void requestMissedTxs(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::crypto::HashListPtr _missedTxs, bcos::protocol::Block::Ptr _verifiedProposal,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) = 0;

    virtual void onRecvSyncMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, std::function<void(bytesConstRef _response)> _sendResponse) = 0;

    virtual TransactionSyncConfig::Ptr config() { return m_config; }
    virtual void onEmptyTxs() = 0;

protected:
    TransactionSyncConfig::Ptr m_config;
};
}  // namespace sync
}  // namespace bcos