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
 * @brief  Storage for the ledger
 * @file LedgerStorage.h
 * @author: yujiechen
 * @date 2021-04-26
 */
#pragma once
#include "../interfaces/PBFTMessageFactory.h"
#include "../interfaces/PBFTStorage.h"
#include <bcos-framework/interfaces/dispatcher/SchedulerInterface.h>
#include <bcos-framework/interfaces/protocol/BlockFactory.h>
#include <bcos-framework/interfaces/storage/KVStorageHelper.h>
#include <bcos-utilities/ThreadPool.h>

namespace bcos
{
namespace consensus
{
class LedgerStorage : public PBFTStorage, public std::enable_shared_from_this<LedgerStorage>
{
public:
    using Ptr = std::shared_ptr<LedgerStorage>;
    LedgerStorage(bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        std::shared_ptr<bcos::storage::KVStorageHelper> _storage,
        bcos::protocol::BlockFactory::Ptr _blockFactory, PBFTMessageFactory::Ptr _messageFactory)
      : m_scheduler(_scheduler),
        m_storage(_storage),
        m_blockFactory(_blockFactory),
        m_messageFactory(_messageFactory)
    {
        createKVTable(m_pbftCommitDB);
        m_commitBlockWorker = std::make_shared<ThreadPool>("blockSubmit", 1);
    }

    void createKVTable(std::string const& _dbName);
    PBFTProposalListPtr loadState(bcos::protocol::BlockNumber _stabledIndex) override;

    // commit the committed proposal into the kv-storage
    void asyncCommitProposal(PBFTProposalInterface::Ptr _proposal) override;
    // commit the executed-block into the blockchain
    void asyncCommitStableCheckPoint(PBFTProposalInterface::Ptr _stableProposal) override;
    void registerFinalizeHandler(
        std::function<void(bcos::ledger::LedgerConfig::Ptr, bool _syncBlock)> _finalizeHandler)
        override
    {
        m_finalizeHandler = _finalizeHandler;
    }
    void asyncGetCommittedProposals(bcos::protocol::BlockNumber _start, size_t _offset,
        std::function<void(PBFTProposalListPtr)> _onSuccess) override;

    int64_t maxCommittedProposalIndex() override { return m_maxCommittedProposalIndex; }

    void asyncRemoveStabledCheckPoint(size_t _stabledCheckPointIndex) override;

protected:
    virtual void asyncPutProposal(std::string const& _dbName, std::string const& _key,
        bytesPointer _committedData, bcos::protocol::BlockNumber _proposalIndex,
        size_t _retryTime = 0);

    virtual void asyncRemove(std::string const& _dbName, std::string const& _key);

    virtual void commitStableCheckPoint(
        bcos::protocol::BlockHeader::Ptr _blockHeader, bcos::protocol::Block::Ptr _blockInfo);
    virtual void asyncGetLatestCommittedProposalIndex();

protected:
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    std::shared_ptr<bcos::storage::KVStorageHelper> m_storage;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    PBFTMessageFactory::Ptr m_messageFactory;

    std::string m_maxCommittedProposalKey = "max_committed_proposal";
    std::string m_pbftCommitDB = "pbftCommitDB";


    std::atomic<int64_t> m_maxCommittedProposalIndex = {0};
    std::atomic_bool m_maxCommittedProposalIndexFetched = {false};

    PBFTProposalListPtr m_stateProposals = nullptr;
    std::atomic_bool m_stateFetched = {false};
    size_t m_timeout = 10000;
    bcos::protocol::BlockNumber c_reservedCheckPointSize = 5;

    boost::condition_variable m_signalled;
    boost::mutex x_signalled;
    std::function<void(bcos::ledger::LedgerConfig::Ptr, bool _syncBlock)> m_finalizeHandler;

    std::shared_ptr<ThreadPool> m_commitBlockWorker;
};
}  // namespace consensus
}  // namespace bcos