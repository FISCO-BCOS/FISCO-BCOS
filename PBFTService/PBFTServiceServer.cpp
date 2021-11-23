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
 * @file PBFTServiceServer.h
 * @author: yujiechen
 * @date 2021-06-29
 */

#include "PBFTServiceServer.h"
#include "Common/TarsUtils.h"

using namespace bcostars;
using namespace bcos::consensus;
using namespace bcos::initializer;

Error PBFTServiceServer::asyncCheckBlock(
    const Block& _block, tars::Bool&, tars::TarsCurrentPtr _current)
{
    auto blockFactory = m_pbftInitializer->blockFactory();
    _current->setResponse(false);
    auto block = std::make_shared<bcostars::protocol::BlockImpl>(
        blockFactory->transactionFactory(), blockFactory->receiptFactory());
    block->setInner(std::move(*const_cast<bcostars::Block*>(&_block)));
    m_pbftInitializer->pbft()->asyncCheckBlock(
        block, [_current](bcos::Error::Ptr _error, bool _verifyResult) {
            async_response_asyncCheckBlock(_current, toTarsError(_error), _verifyResult);
        });
    return bcostars::Error();
}

Error PBFTServiceServer::asyncGetPBFTView(tars::Int64& _view, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_pbftInitializer->pbft()->asyncGetPBFTView(
        [_current](bcos::Error::Ptr _error, bcos::consensus::ViewType _view) {
            async_response_asyncGetPBFTView(_current, toTarsError(_error), _view);
        });
    return bcostars::Error();
}


Error PBFTServiceServer::asyncNoteUnSealedTxsSize(
    tars::Int64 _unsealedTxsSize, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_pbftInitializer->sealer()->asyncNoteUnSealedTxsSize(
        _unsealedTxsSize, [_current](bcos::Error::Ptr _error) {
            async_response_asyncNoteUnSealedTxsSize(_current, toTarsError(_error));
        });
    return bcostars::Error();
}

Error PBFTServiceServer::asyncNotifyConsensusMessage(std::string const& _uuid,
    const vector<tars::Char>& _nodeId, const vector<tars::Char>& _data,
    tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto nodeId = m_pbftInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((const bcos::byte*)_nodeId.data(), _nodeId.size()));
    m_pbftInitializer->pbft()->asyncNotifyConsensusMessage(nullptr, _uuid, nodeId,
        bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()),
        [_current](bcos::Error::Ptr _error) {
            async_response_asyncNotifyConsensusMessage(_current, toTarsError(_error));
        });
    return bcostars::Error();
}

bcostars::Error PBFTServiceServer::asyncNotifyBlockSyncMessage(std::string const& _uuid,
    const vector<tars::Char>& _nodeId, const vector<tars::Char>& _data,
    tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto nodeId = m_pbftInitializer->keyFactory()->createKey(
        bcos::bytesConstRef((const bcos::byte*)_nodeId.data(), _nodeId.size()));
    m_pbftInitializer->blockSync()->asyncNotifyBlockSyncMessage(nullptr, _uuid, nodeId,
        bcos::bytesConstRef((const bcos::byte*)_data.data(), _data.size()),
        [_current](bcos::Error::Ptr _error) {
            async_response_asyncNotifyBlockSyncMessage(_current, toTarsError(_error));
        });
    return bcostars::Error();
}


Error PBFTServiceServer::asyncNotifyNewBlock(
    const LedgerConfig& _ledgerConfig, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto ledgerConfig = toLedgerConfig(_ledgerConfig, m_pbftInitializer->keyFactory());
    m_pbftInitializer->pbft()->asyncNotifyNewBlock(
        ledgerConfig, [_current](bcos::Error::Ptr _error) {
            async_response_asyncNotifyNewBlock(_current, toTarsError(_error));
        });
    return bcostars::Error();
}

Error PBFTServiceServer::asyncSubmitProposal(bool _containSysTxs,
    const vector<tars::Char>& _proposalData, tars::Int64 _proposalIndex,
    const vector<tars::Char>& _proposalHash, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    auto proposalHash = bcos::crypto::HashType();
    if (_proposalHash.size() >= bcos::crypto::HashType::size)
    {
        proposalHash = bcos::crypto::HashType(
            (const bcos::byte*)_proposalHash.data(), bcos::crypto::HashType::size);
    }
    m_pbftInitializer->pbft()->asyncSubmitProposal(_containSysTxs,
        bcos::bytesConstRef((const bcos::byte*)_proposalData.data(), _proposalData.size()),
        _proposalIndex, proposalHash, [_current](bcos::Error::Ptr _error) {
            async_response_asyncSubmitProposal(_current, toTarsError(_error));
        });
    return bcostars::Error();
}

bcostars::Error PBFTServiceServer::asyncGetSyncInfo(std::string&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_pbftInitializer->blockSync()->asyncGetSyncInfo(
        [_current](bcos::Error::Ptr _error, std::string const& _syncInfo) {
            async_response_asyncGetSyncInfo(_current, toTarsError(_error), _syncInfo);
        });
    return bcostars::Error();
}

bcostars::Error PBFTServiceServer::asyncGetConsensusStatus(
    std::string&, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_pbftInitializer->pbft()->asyncGetConsensusStatus(
        [_current](bcos::Error::Ptr _error, std::string _consensusStatus) {
            async_response_asyncGetConsensusStatus(_current, toTarsError(_error), _consensusStatus);
        });
    return bcostars::Error();
}