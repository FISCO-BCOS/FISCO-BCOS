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
 * @brief interface for Consensus module
 * @file ConsensusInterface.h
 * @author: yujiechen
 * @date 2021-04-08
 */
#pragma once
#include "../../interfaces/ledger/LedgerConfig.h"
#include "../../interfaces/protocol/Block.h"
#include "../../interfaces/protocol/ProtocolTypeDef.h"
#include "ConsensusTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-utilities/Error.h>

namespace bcos
{
namespace consensus
{
// ConsensusInterface is the interface of consensus exposed to other modules
class ConsensusInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusInterface>;
    ConsensusInterface() = default;
    virtual ~ConsensusInterface() {}

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void asyncSubmitProposal(bool _containSysTxs, bytesConstRef _proposalData,
        bcos::protocol::BlockNumber _proposalIndex, bcos::crypto::HashType const& _proposalHash,
        std::function<void(Error::Ptr)> _onProposalSubmitted) = 0;

    virtual void asyncGetPBFTView(std::function<void(Error::Ptr, ViewType)> _onGetView) = 0;

    // the sync module calls this interface to check block
    virtual void asyncCheckBlock(bcos::protocol::Block::Ptr _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinish) = 0;
    // the sync module calls this interface to notify new block
    virtual void asyncNotifyNewBlock(
        bcos::ledger::LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv) = 0;

    // called by frontService to dispatch message
    virtual void asyncNotifyConsensusMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) = 0;


    // for the sync module to notify the syncing number
    virtual void notifyHighestSyncingNumber(bcos::protocol::BlockNumber _number) = 0;

    virtual void asyncNoteUnSealedTxsSize(
        uint64_t _unsealedTxsSize, std::function<void(Error::Ptr)> _onRecvResponse) = 0;

    // get the consensusNodeList
    // Note: if separate sealer with the PBFT module, should implement with notify
    virtual ConsensusNodeList consensusNodeList() const { return ConsensusNodeList(); }
    virtual uint64_t nodeIndex() const { return 0; }

    virtual void asyncGetConsensusStatus(
        std::function<void(Error::Ptr, std::string)> _onGetConsensusStatus) = 0;
    virtual void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) = 0;
};
}  // namespace consensus
}  // namespace bcos
