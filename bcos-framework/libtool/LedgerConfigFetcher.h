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
 * @brief Public function to get information from Ledger
 * @file LedgerConfigFetcher.h
 * @author: yujiechen
 * @date 2021-05-19
 */
#pragma once
#include "../interfaces/ledger/LedgerConfig.h"
#include "../interfaces/ledger/LedgerInterface.h"
#include "bcos-utilities/Log.h"

#define TOOL_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TOOL")

namespace bcos
{
namespace tool
{
class LedgerConfigFetcher : public std::enable_shared_from_this<LedgerConfigFetcher>
{
public:
    using Ptr = std::shared_ptr<LedgerConfigFetcher>;
    explicit LedgerConfigFetcher(bcos::ledger::LedgerInterface::Ptr _ledger)
      : m_ledger(_ledger), m_ledgerConfig(std::make_shared<bcos::ledger::LedgerConfig>())
    {}

    virtual ~LedgerConfigFetcher() {}

    virtual void fetchBlockNumberAndHash(size_t _fetchedTime = 0);
    virtual void fetchConsensusNodeList();
    virtual void fetchObserverNodeList();
    virtual void fetchBlockTxCountLimit();
    virtual void fetchGenesisHash();
    virtual void fetchNonceList(
        protocol::BlockNumber _startNumber, int64_t _offset, size_t _fetchedTime = 0);
    virtual void fetchConsensusLeaderPeriod();

    // consensus_leader_period
    virtual bcos::ledger::LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }
    virtual std::shared_ptr<std::map<int64_t, bcos::protocol::NonceListPtr>> nonceList()
    {
        return m_nonceList;
    }
    virtual bcos::crypto::HashType const& genesisHash() const { return m_genesisHash; }
    virtual void waitFetchFinished();

protected:
    virtual bool fetchFinished()
    {
        return m_fetchBlockInfoFinished && m_fetchConsensusInfoFinished &&
               m_fetchObserverInfoFinshed && m_fetchBlockTxCountLimitFinished &&
               m_fetchNonceListFinished && m_fetchGenesisHashFinished &&
               m_fetchConsensusLeaderPeriod;
    }

    virtual void fetchBlockHash(bcos::protocol::BlockNumber _blockNumber,
        std::function<void(bcos::crypto::HashType const& _hash)> _callback,
        size_t _fetchedTime = 0);
    virtual void fetchSystemConfig(std::string const& _key,
        std::function<void(std::string const&)> _onRecvValue, size_t _fetchedTime = 0);
    virtual void fetchNodeListByNodeType(std::string const& _type,
        bcos::consensus::ConsensusNodeListPtr _nodeList, std::function<void()> _onRecvNodeList,
        size_t _fetchedTime = 0);

    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::ledger::LedgerConfig::Ptr m_ledgerConfig;
    std::shared_ptr<std::map<int64_t, bcos::protocol::NonceListPtr>> m_nonceList;
    bcos::crypto::HashType m_genesisHash;

    // assume this module can wait at most 10s
    uint64_t m_timeout = 10000;

private:
    boost::condition_variable m_signalled;
    boost::mutex x_signalled;

    std::atomic_bool m_fetchBlockInfoFinished = {true};
    std::atomic_bool m_fetchConsensusInfoFinished = {true};
    std::atomic_bool m_fetchObserverInfoFinshed = {true};
    std::atomic_bool m_fetchBlockTxCountLimitFinished = {true};
    std::atomic_bool m_fetchNonceListFinished = {true};
    std::atomic_bool m_fetchGenesisHashFinished = {true};
    std::atomic_bool m_fetchConsensusLeaderPeriod = {true};

    size_t c_maxRetryTime = 5;
};
}  // namespace tool
}  // namespace bcos