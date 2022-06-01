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
 * @brief server for the PBFTService
 * @file PBFTServiceServers.h
 * @author: yujiechen
 * @date 2021-06-29
 */

#pragma once
#include "libinitializer/PBFTInitializer.h"
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/tars/PBFTService.h>
#include <servant/Application.h>
#include <servant/Communicator.h>
#include <mutex>

namespace bcostars
{
struct PBFTServiceParam
{
    bcos::initializer::PBFTInitializer::Ptr pbftInitializer;
};
class PBFTServiceServer : public bcostars::PBFTService
{
public:
    using Ptr = std::shared_ptr<PBFTServiceServer>;
    PBFTServiceServer(PBFTServiceParam const& _param) : m_pbftInitializer(_param.pbftInitializer) {}
    ~PBFTServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    // for the sync module to check block
    // Node: since the sync module is integrated with the PBFT, this interfaces is useless now
    bcostars::Error asyncCheckBlock(
        const bcostars::Block& _block, tars::Bool&, tars::TarsCurrentPtr _current) override;

    // for the rpc module to get the pbft view
    bcostars::Error asyncGetPBFTView(tars::Int64& _view, tars::TarsCurrentPtr _current) override;
    bcostars::Error asyncGetSyncInfo(
        std::string& _syncInfo, tars::TarsCurrentPtr _current) override;

    // Note: since the sealer is integrated with the PBFT, this interfaces is useless now
    bcostars::Error asyncNoteUnSealedTxsSize(
        tars::Int64 _unsealedTxsSize, tars::TarsCurrentPtr _current) override;

    bcostars::Error asyncNotifyConsensusMessage(std::string const& _uuid,
        const vector<tars::Char>& _nodeId, const vector<tars::Char>& _data,
        tars::TarsCurrentPtr _current) override;

    bcostars::Error asyncNotifyBlockSyncMessage(std::string const& _uuid,
        const vector<tars::Char>& _nodeId, const vector<tars::Char>& _data,
        tars::TarsCurrentPtr _current) override;

    // Note: since the blockSync module is integrated with the PBFT, this interfaces is useless now
    bcostars::Error asyncNotifyNewBlock(
        const bcostars::LedgerConfig& _ledgerConfig, tars::TarsCurrentPtr _current) override;

    // Note: since the sealer module is integrated with the PBFT, the interface is useless now
    bcostars::Error asyncSubmitProposal(bool _containSysTxs,
        const vector<tars::Char>& _proposalData, tars::Int64 _proposalIndex,
        const vector<tars::Char>& _proposalHash, tars::TarsCurrentPtr _current) override;

    bcostars::Error asyncNotifyConnectedNodes(
        const vector<vector<tars::Char>>& connectedNodes, tars::TarsCurrentPtr current) override
    {
        current->setResponse(false);

        bcos::crypto::NodeIDSet bcosNodeIDSet;
        for (auto const& it : connectedNodes)
        {
            bcosNodeIDSet.insert(m_pbftInitializer->keyFactory()->createKey(
                bcos::bytesConstRef((const bcos::byte*)it.data(), it.size())));
        }
        m_pbftInitializer->blockSync()->notifyConnectedNodes(
            bcosNodeIDSet, [current](bcos::Error::Ptr error) {
                async_response_asyncNotifyConnectedNodes(current, bcostars::toTarsError(error));
            });
        m_pbftInitializer->pbft()->notifyConnectedNodes(
            bcosNodeIDSet, [](bcos::Error::Ptr error) {});
        return bcostars::Error();
    }

    bcostars::Error asyncGetConsensusStatus(
        std::string& _consensusStatus, tars::TarsCurrentPtr current) override;

private:
    bcos::initializer::PBFTInitializer::Ptr m_pbftInitializer;
};

}  // namespace bcostars