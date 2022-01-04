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
 * @file LedgerConfigFetcher.cpp
 * @author: yujiechen
 * @date 2021-05-19
 */
#include "LedgerConfigFetcher.h"
#include "../interfaces/ledger/LedgerTypeDef.h"
#include "Exceptions.h"
#include "bcos-utilities/Common.h"
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;
using namespace bcos::tool;
using namespace bcos::ledger;

void LedgerConfigFetcher::waitFetchFinished()
{
    auto startT = utcSteadyTime();
    auto fetchSuccess = false;
    while (utcSteadyTime() - startT < m_timeout)
    {
        if (fetchFinished())
        {
            fetchSuccess = true;
            break;
        }
        boost::unique_lock<boost::mutex> l(x_signalled);
        m_signalled.wait_for(l, boost::chrono::milliseconds(10));
    }
    if (!fetchSuccess)
    {
        TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetch failed");
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException() << errinfo_comment(
                                  "LedgerConfigFetcher: fetch ledgerConfig failed "));
    }
    TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetch success");
}

void LedgerConfigFetcher::fetchBlockNumberAndHash(size_t _fetchedTime)
{
    m_fetchBlockInfoFinished = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    m_ledger->asyncGetBlockNumber([self, _fetchedTime](Error::Ptr _error, BlockNumber _number) {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            if (_error == nullptr)
            {
                TOOL_LOG(INFO) << LOG_DESC("fetchBlockNumber success, begin to fetchBlockHash")
                               << LOG_KV("number", _number);
                fetcher->m_ledgerConfig->setBlockNumber(_number);
                fetcher->fetchBlockHash(_number, [fetcher](HashType const& _hash) {
                    fetcher->m_ledgerConfig->setHash(_hash);
                    fetcher->m_fetchBlockInfoFinished = true;
                });
                return;
            }
            // retry to fetchBlockNumberAndHash
            TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchBlockNumber failed")
                              << LOG_KV("errorCode", _error->errorCode())
                              << LOG_KV("errorMessage", _error->errorMessage());
            if (_fetchedTime >= fetcher->c_maxRetryTime)
            {
                return;
            }
            fetcher->fetchBlockNumberAndHash(_fetchedTime + 1);
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchBlockNumberAndHash exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void LedgerConfigFetcher::fetchGenesisHash()
{
    m_fetchGenesisHashFinished = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    fetchBlockHash(0, [self](HashType const& _hash) {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            fetcher->m_genesisHash = _hash;
            fetcher->m_fetchGenesisHashFinished = true;
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchGenesisHash exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void LedgerConfigFetcher::fetchBlockHash(BlockNumber _blockNumber,
    std::function<void(HashType const& _hash)> _callback, size_t _fetchedTime)
{
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    m_ledger->asyncGetBlockHashByNumber(_blockNumber,
        [self, _blockNumber, _callback, _fetchedTime](Error::Ptr _error, HashType _hash) {
            try
            {
                auto fetcher = self.lock();
                if (!fetcher)
                {
                    return;
                }
                if (_error == nullptr)
                {
                    TOOL_LOG(INFO)
                        << LOG_DESC("LedgerConfigFetcher: fetchBlockHash success")
                        << LOG_KV("number", _blockNumber) << LOG_KV("hash", _hash.abridged());
                    _callback(_hash);
                    fetcher->m_signalled.notify_all();
                    return;
                }
                if (_fetchedTime >= fetcher->c_maxRetryTime)
                {
                    return;
                }
                // retry to  fetchBlockHash
                TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchBlockHash failed")
                                  << LOG_KV("errorCode", _error->errorCode())
                                  << LOG_KV("errorMessage", _error->errorMessage())
                                  << LOG_KV("number", _blockNumber);
                fetcher->fetchBlockHash(_blockNumber, _callback, (_fetchedTime + 1));
            }
            catch (std::exception const& e)
            {
                TOOL_LOG(WARNING) << LOG_DESC("fetchBlockHash exception")
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}


void LedgerConfigFetcher::fetchSystemConfig(std::string const& _key,
    std::function<void(std::string const&)> _onRecvValue, size_t _fetchedTime)
{
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    m_ledger->asyncGetSystemConfigByKey(
        _key, [self, _key, _onRecvValue, _fetchedTime](
                  Error::Ptr _error, std::string _sysValue, BlockNumber _blockNumber) {
            try
            {
                auto fetcher = self.lock();
                if (!fetcher)
                {
                    return;
                }
                if (_error == nullptr)
                {
                    TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetchSystemConfig success")
                                   << LOG_KV("key", _key) << LOG_KV("value", _sysValue)
                                   << LOG_KV("number", _blockNumber);
                    _onRecvValue(_sysValue);
                    return;
                }
                // retry to fetchSystemConfig
                TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchSystemConfig failed")
                                  << LOG_KV("errorCode", _error->errorCode())
                                  << LOG_KV("errorMessage", _error->errorMessage())
                                  << LOG_KV("key", _key);
                if (_fetchedTime >= fetcher->c_maxRetryTime)
                {
                    return;
                }
                fetcher->fetchSystemConfig(_key, _onRecvValue, (_fetchedTime + 1));
            }
            catch (std::exception const& e)
            {
                TOOL_LOG(WARNING) << LOG_DESC("fetchSystemConfig exception")
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void LedgerConfigFetcher::fetchNodeListByNodeType(std::string const& _type,
    ConsensusNodeListPtr _nodeList, std::function<void()> _onRecvNodeList, size_t _fetchedTime)
{
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    m_ledger->asyncGetNodeListByType(_type, [self, _type, _nodeList, _onRecvNodeList, _fetchedTime](
                                                Error::Ptr _error, ConsensusNodeListPtr _nodes) {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            if (_error == nullptr)
            {
                TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetchNodeListByNodeType success")
                               << LOG_KV("nodesSize", _nodes->size()) << LOG_KV("type", _type);
                *_nodeList = *_nodes;
                _onRecvNodeList();
                return;
            }
            // retry to  fetchNodeListByNodeType
            TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchNodeListByNodeType failed")
                              << LOG_KV("errorCode", _error->errorCode())
                              << LOG_KV("errorMessage", _error->errorMessage())
                              << LOG_KV("type", _type);
            if (_fetchedTime >= fetcher->c_maxRetryTime)
            {
                return;
            }
            fetcher->fetchNodeListByNodeType(_type, _nodeList, _onRecvNodeList, (_fetchedTime + 1));
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchNodeListByNodeType exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void LedgerConfigFetcher::fetchConsensusNodeList()
{
    m_fetchConsensusInfoFinished = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    fetchNodeListByNodeType(CONSENSUS_SEALER, m_ledgerConfig->mutableConsensusList(), [self]() {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            fetcher->m_fetchConsensusInfoFinished = true;
            fetcher->m_signalled.notify_all();
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchConsensusNodeList exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

void LedgerConfigFetcher::fetchObserverNodeList()
{
    m_fetchObserverInfoFinshed = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    fetchNodeListByNodeType(CONSENSUS_OBSERVER, m_ledgerConfig->mutableObserverList(), [self]() {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            fetcher->m_fetchObserverInfoFinshed = true;
            fetcher->m_signalled.notify_all();
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchObserverNodeList exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}
void LedgerConfigFetcher::fetchConsensusLeaderPeriod()
{
    m_fetchConsensusLeaderPeriod = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    fetchSystemConfig(
        SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, [self](std::string const& _consensusLeaderPeriod) {
            try
            {
                auto fetcher = self.lock();
                if (!fetcher)
                {
                    return;
                }
                fetcher->m_ledgerConfig->setLeaderSwitchPeriod(
                    boost::lexical_cast<uint64_t>(_consensusLeaderPeriod));
                fetcher->m_fetchConsensusLeaderPeriod = true;
                fetcher->m_signalled.notify_all();
            }
            catch (std::exception const& e)
            {
                TOOL_LOG(WARNING) << LOG_DESC("fetchConsensusLeaderPeriod exception")
                                  << LOG_KV("error", boost::diagnostic_information(e))
                                  << LOG_KV("consensusLeaderPeriod", _consensusLeaderPeriod);
            }
        });
}
void LedgerConfigFetcher::fetchBlockTxCountLimit()
{
    m_fetchBlockTxCountLimitFinished = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    fetchSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT, [self](std::string const& _blockTxCountLimit) {
        try
        {
            auto fetcher = self.lock();
            if (!fetcher)
            {
                return;
            }
            fetcher->m_ledgerConfig->setBlockTxCountLimit(
                boost::lexical_cast<uint64_t>(_blockTxCountLimit));
            fetcher->m_fetchBlockTxCountLimitFinished = true;
            fetcher->m_signalled.notify_all();
        }
        catch (std::exception const& e)
        {
            TOOL_LOG(WARNING) << LOG_DESC("fetchBlockTxCountLimit exception")
                              << LOG_KV("error", boost::diagnostic_information(e))
                              << LOG_KV("blockTxCountLimit", _blockTxCountLimit);
        }
    });
}

void LedgerConfigFetcher::fetchNonceList(
    BlockNumber _startNumber, int64_t _offset, size_t _fetchedTime)
{
    m_fetchNonceListFinished = false;
    auto self = std::weak_ptr<LedgerConfigFetcher>(shared_from_this());
    m_ledger->asyncGetNonceList(_startNumber, _offset,
        [self, _startNumber, _offset, _fetchedTime](
            Error::Ptr _error, std::shared_ptr<std::map<BlockNumber, NonceListPtr>> _nonceList) {
            try
            {
                auto fetcher = self.lock();
                if (!fetcher)
                {
                    return;
                }
                if (_error == nullptr)
                {
                    TOOL_LOG(INFO)
                        << LOG_DESC("LedgerConfigFetcher: fetchNonceList success")
                        << LOG_KV("startNumber", _startNumber) << LOG_KV("offset", _offset);
                    fetcher->m_nonceList = _nonceList;
                    fetcher->m_fetchNonceListFinished = true;
                    fetcher->m_signalled.notify_all();
                    return;
                }
                TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchNonceList failed")
                                  << LOG_KV("errorCode", _error->errorCode())
                                  << LOG_KV("errorMsg", _error->errorMessage())
                                  << LOG_KV("startNumber", _startNumber)
                                  << LOG_KV("offset", _offset);
                if (_fetchedTime >= fetcher->c_maxRetryTime)
                {
                    return;
                }
                fetcher->fetchNonceList(_startNumber, _offset, (_fetchedTime + 1));
            }
            catch (std::exception const& e)
            {
                TOOL_LOG(WARNING) << LOG_DESC("fetchNonceList exception")
                                  << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}