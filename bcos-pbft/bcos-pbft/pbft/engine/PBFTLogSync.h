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
 * @brief implementation for PBFT log syncing
 * @file PBFTLogSync.h
 * @author: yujiechen
 * @date 2021-04-28
 */
#pragma once
#include "../cache/PBFTCacheProcessor.h"
#include "../config/PBFTConfig.h"
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-utilities/ThreadPool.h>
namespace bcos
{
namespace consensus
{
class PBFTLogSync : public std::enable_shared_from_this<PBFTLogSync>
{
public:
    using Ptr = std::shared_ptr<PBFTLogSync>;
    PBFTLogSync(PBFTConfig::Ptr _config, PBFTCacheProcessor::Ptr _pbftCache);
    virtual ~PBFTLogSync() {}
    using SendResponseCallback = std::function<void(bytesConstRef _respData)>;
    using HandlePrePrepareCallback = std::function<void(PBFTMessageInterface::Ptr)>;
    /**
     * @brief batch request committed proposals to the given node
     *
     * @param _from the node that maintains the requested proposals
     * @param _startIndex the start index of the request
     * @param _offset the requested proposal size
     */
    virtual void requestCommittedProposals(
        bcos::crypto::PublicPtr _from, bcos::protocol::BlockNumber _startIndex, size_t _offset);

    /**
     * @brief request precommit data from the given node
     *
     * @param _from the node that maintain the precommit data
     * @param _index the index of the requested precommit data
     * @param _hash  the hash of the requested precommit data
     */
    virtual void requestPrecommitData(bcos::crypto::PublicPtr _from,
        PBFTMessageInterface::Ptr _prePrepareMsg, HandlePrePrepareCallback _prePrepareCallback);

    virtual void stop() { m_requestThread->stop(); }

protected:
    virtual void onRecvCommittedProposalsResponse(bcos::Error::Ptr _error,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        bcos::protocol::BlockNumber _startIndex, size_t _offset,
        SendResponseCallback _sendResponse);

    virtual void onRecvPrecommitResponse(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, PBFTMessageInterface::Ptr _prePrepareMsg,
        HandlePrePrepareCallback _prePrepareCallback, SendResponseCallback _sendResponse);

    void requestPBFTData(bcos::crypto::PublicPtr _from, PBFTRequestInterface::Ptr _pbftRequest,
        bcos::front::CallbackFunc _callback);

private:
    PBFTConfig::Ptr m_config;
    PBFTCacheProcessor::Ptr m_pbftCache;
    std::shared_ptr<ThreadPool> m_requestThread;
};
}  // namespace consensus
}  // namespace bcos