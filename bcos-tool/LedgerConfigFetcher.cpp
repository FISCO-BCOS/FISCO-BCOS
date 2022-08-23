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
#include "Exceptions.h"
#include "VersionConverter.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-utilities/Common.h>
#include <future>
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;
using namespace bcos::tool;
using namespace bcos::ledger;

void LedgerConfigFetcher::fetchBlockNumberAndHash()
{
    std::promise<std::pair<Error::Ptr, BlockNumber>> blockNumberPromise;
    m_ledger->asyncGetBlockNumber([&blockNumberPromise](Error::Ptr _error, BlockNumber _number) {
        blockNumberPromise.set_value(std::make_pair(_error, _number));
    });
    auto ret = blockNumberPromise.get_future().get();
    auto error = ret.first;
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchBlockNumber failed")
                          << LOG_KV("errorCode", error->errorCode())
                          << LOG_KV("errorMessage", error->errorMessage());
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException()
                              << errinfo_comment("LedgerConfigFetcher: fetchBlockNumber failed "));
    }
    auto blockNumber = ret.second;
    m_ledgerConfig->setBlockNumber(blockNumber);
    TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetchBlockNumber success")
                   << LOG_KV("blockNumber", blockNumber);
    // fetch blockHash
    auto hash = fetchBlockHash(blockNumber);
    TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetchBlockHash success")
                   << LOG_KV("blockNumber", blockNumber) << LOG_KV("hash", hash.abridged());
    m_ledgerConfig->setHash(hash);
}

void LedgerConfigFetcher::fetchGenesisHash()
{
    m_genesisHash = fetchBlockHash(0);
    TOOL_LOG(INFO) << LOG_DESC("fetchGenesisHash success")
                   << LOG_KV("genesisHash", m_genesisHash.abridged());
}

HashType LedgerConfigFetcher::fetchBlockHash(BlockNumber _blockNumber)
{
    std::promise<std::pair<Error::Ptr, HashType>> hashPromise;
    m_ledger->asyncGetBlockHashByNumber(
        _blockNumber, [&hashPromise](Error::Ptr _error, HashType _hash) {
            hashPromise.set_value(std::make_pair(_error, _hash));
        });
    auto result = hashPromise.get_future().get();
    auto error = result.first;
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchBlockHash failed")
                          << LOG_KV("errorCode", error->errorCode())
                          << LOG_KV("errorMessage", error->errorMessage())
                          << LOG_KV("number", _blockNumber);
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException()
                              << errinfo_comment("LedgerConfigFetcher: fetchBlockHash failed "));
    }
    return result.second;
}


std::string LedgerConfigFetcher::fetchSystemConfig(std::string_view _key)
{
    std::promise<std::tuple<Error::Ptr, std::string, BlockNumber>> systemConfigPromise;
    m_ledger->asyncGetSystemConfigByKey(_key,
        [&systemConfigPromise](Error::Ptr _error, std::string _sysValue, BlockNumber _blockNumber) {
            systemConfigPromise.set_value(std::tuple(_error, _sysValue, _blockNumber));
        });
    auto ret = systemConfigPromise.get_future().get();
    auto error = std::get<0>(ret);
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("fetchSystemConfig failed")
                          << LOG_KV("errorCode", error->errorCode())
                          << LOG_KV("errorMessage", error->errorMessage()) << LOG_KV("key", _key);
        BOOST_THROW_EXCEPTION(
            LedgerConfigFetcherException()
            << errinfo_comment("LedgerConfigFetcher: fetchSystemConfig for " + std::string{_key} + " failed"));
    }
    return std::get<1>(ret);
}

ConsensusNodeListPtr LedgerConfigFetcher::fetchNodeListByNodeType(std::string_view _type)
{
    std::promise<std::pair<Error::Ptr, ConsensusNodeListPtr>> nodeListPromise;
    m_ledger->asyncGetNodeListByType(
        _type, [&nodeListPromise](Error::Ptr _error, ConsensusNodeListPtr _nodes) {
            nodeListPromise.set_value(std::make_pair(_error, _nodes));
        });
    auto ret = nodeListPromise.get_future().get();
    auto error = ret.first;
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("fetchNodeListByNodeType failed") << LOG_KV("type", _type)
                          << LOG_KV("code", error->errorCode())
                          << LOG_KV("msg", error->errorMessage());
        BOOST_THROW_EXCEPTION(
            LedgerConfigFetcherException() << errinfo_comment(
                "LedgerConfigFetcher: fetchNodeListByNodeType of type " + std::string{_type} + " failed"));
    }
    return ret.second;
}

void LedgerConfigFetcher::fetchConsensusNodeList()
{
    auto consensusNodeList = fetchNodeListByNodeType(CONSENSUS_SEALER);
    TOOL_LOG(INFO) << LOG_DESC("fetchConsensusNodeList success")
                   << LOG_KV("size", consensusNodeList->size());
    m_ledgerConfig->setConsensusNodeList(*consensusNodeList);
}

void LedgerConfigFetcher::fetchObserverNodeList()
{
    auto observerList = fetchNodeListByNodeType(CONSENSUS_OBSERVER);
    TOOL_LOG(INFO) << LOG_DESC("fetchObserverNodeList success")
                   << LOG_KV("size", observerList->size());
    m_ledgerConfig->setObserverNodeList(*observerList);
}

void LedgerConfigFetcher::fetchConsensusLeaderPeriod()
{
    auto ret = fetchSystemConfig(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD);
    TOOL_LOG(INFO) << LOG_DESC("fetchConsensusLeaderPeriod success") << LOG_KV("value", ret);
    m_ledgerConfig->setLeaderSwitchPeriod(boost::lexical_cast<uint64_t>(ret));
}

void LedgerConfigFetcher::fetchBlockTxCountLimit()
{
    auto ret = fetchSystemConfig(SYSTEM_KEY_TX_COUNT_LIMIT);
    TOOL_LOG(INFO) << LOG_DESC("fetchBlockTxCountLimit success") << LOG_KV("value", ret);
    m_ledgerConfig->setBlockTxCountLimit(boost::lexical_cast<uint64_t>(ret));
}

void LedgerConfigFetcher::fetchNonceList(BlockNumber _startNumber, int64_t _offset)
{
    std::promise<std::pair<Error::Ptr, std::shared_ptr<std::map<BlockNumber, NonceListPtr>>>>
        noncePromise;
    m_ledger->asyncGetNonceList(_startNumber, _offset,
        [&noncePromise](
            Error::Ptr _error, std::shared_ptr<std::map<BlockNumber, NonceListPtr>> _nonceList) {
            noncePromise.set_value(std::make_pair(_error, _nonceList));
        });
    auto ret = noncePromise.get_future().get();
    auto error = ret.first;
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("LedgerConfigFetcher: fetchNonceList failed")
                          << LOG_KV("errorCode", error->errorCode())
                          << LOG_KV("errorMsg", error->errorMessage())
                          << LOG_KV("startNumber", _startNumber) << LOG_KV("offset", _offset);
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException() << errinfo_comment(
                                  "LedgerConfigFetcher: fetchNonceList failed, start: " +
                                  boost::lexical_cast<std::string>(_startNumber) +
                                  ", offset:" + boost::lexical_cast<std::string>(_offset)));
    }
    m_nonceList = ret.second;
}

void LedgerConfigFetcher::fetchCompatibilityVersion()
{
    TOOL_LOG(INFO) << LOG_DESC("fetchCompatibilityVersion");
    auto versionStr = fetchSystemConfig(SYSTEM_KEY_COMPATIBILITY_VERSION);
    if (versionStr.size() == 0)
    {
        m_ledgerConfig->setCompatibilityVersion((uint32_t)(bcos::protocol::DEFAULT_VERSION));
        TOOL_LOG(INFO) << LOG_DESC("fetchCompatibilityVersion: empty version, use " +
                                   bcos::protocol::RC4_VERSION_STR + " as default version.");
        return;
    }
    auto version = toVersionNumber(versionStr);
    m_ledgerConfig->setCompatibilityVersion(version);
    TOOL_LOG(INFO) << LOG_DESC("fetchCompatibilityVersion success") << LOG_KV("version", versionStr)
                   << LOG_KV("versionNumber", version)
                   << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion())
                   << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion());
}