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
#include <bcos-executor/src/Common.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-utilities/Common.h>
#include <future>
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;
using namespace bcos::tool;
using namespace bcos::ledger;

void LedgerConfigFetcher::fetchAll()
{
    // should not change fetch order
    fetchBlockNumberAndHash();
    fetchCompatibilityVersion();
    fetchFeatures();
    fetchConsensusNodeList();
    fetchObserverNodeList();
    fetchCandidateSealerList();
    fetchBlockTxCountLimit();
    fetchGenesisHash();
    fetchConsensusLeaderPeriod();
    fetchAuthCheckStatus();
    fetchEpochSealerNum();
    fetchEpochBlockNum();
    fetchNotifyRotateFlagInfo();
}

void LedgerConfigFetcher::fetchBlockNumber()
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
                          << LOG_KV("code", error->errorCode())
                          << LOG_KV("message", error->errorMessage());
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException()
                              << errinfo_comment("LedgerConfigFetcher: fetchBlockNumber failed "));
    }
    auto blockNumber = ret.second;
    m_ledgerConfig->setBlockNumber(blockNumber);
    TOOL_LOG(INFO) << LOG_DESC("LedgerConfigFetcher: fetchBlockNumber success")
                   << LOG_KV("blockNumber", blockNumber);
}

void LedgerConfigFetcher::fetchBlockNumberAndHash()
{
    fetchBlockNumber();
    auto blockNumber = m_ledgerConfig->blockNumber();
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
                          << LOG_KV("code", error->errorCode())
                          << LOG_KV("message", error->errorMessage())
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
        TOOL_LOG(INFO) << LOG_DESC("fetchSystemConfig failed") << LOG_KV("code", error->errorCode())
                       << LOG_KV("message", error->errorMessage()) << LOG_KV("key", _key);
        BOOST_THROW_EXCEPTION(
            LedgerConfigFetcherException() << errinfo_comment(
                "LedgerConfigFetcher: fetchSystemConfig for " + std::string{_key} + " failed"));
    }
    return std::get<1>(ret);
}

bcos::ledger::SystemConfigEntry LedgerConfigFetcher::fetchSystemConfigNoException(
    std::string_view _key, bcos::ledger::SystemConfigEntry _defaultValue) noexcept
{
    std::promise<std::tuple<Error::Ptr, bcos::ledger::SystemConfigEntry>> systemConfigPromise;
    m_ledger->asyncGetSystemConfigByKey(_key, [&systemConfigPromise](const Error::Ptr& _error,
                                                  std::string _sysValue, BlockNumber _blockNumber) {
        systemConfigPromise.set_value({_error, {std::move(_sysValue), _blockNumber}});
    });
    auto ret = systemConfigPromise.get_future().get();
    auto error = std::get<0>(ret);
    if (error)
    {
        TOOL_LOG(INFO) << LOG_DESC("fetchSystemConfig failed, use default value")
                       << LOG_KV("code", error->errorCode()) << LOG_KV("msg", error->errorMessage())
                       << LOG_KV("key", _key) << LOG_KV("defaultValue", std::get<0>(_defaultValue));
        return _defaultValue;
    }
    return std::get<SystemConfigEntry>(ret);
}

ConsensusNodeListPtr LedgerConfigFetcher::fetchNodeListByNodeType(std::string_view _type)
{
    std::promise<std::pair<Error::Ptr, ConsensusNodeListPtr>> nodeListPromise;
    m_ledger->asyncGetNodeListByType(
        _type, [&nodeListPromise](Error::Ptr _error, ConsensusNodeListPtr _nodes) {
            nodeListPromise.set_value({std::move(_error), std::move(_nodes)});
        });
    auto ret = nodeListPromise.get_future().get();
    auto error = ret.first;
    if (error)
    {
        TOOL_LOG(WARNING) << LOG_DESC("fetchNodeListByNodeType failed") << LOG_KV("type", _type)
                          << LOG_KV("code", error->errorCode())
                          << LOG_KV("msg", error->errorMessage());
        BOOST_THROW_EXCEPTION(LedgerConfigFetcherException() << errinfo_comment(
                                  "LedgerConfigFetcher: fetchNodeListByNodeType of type " +
                                  std::string{_type} + " failed"));
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

void LedgerConfigFetcher::fetchFeatures()
{
    Features features;
    for (auto const& key : Features::featureKeys())
    {
        auto value = fetchSystemConfigNoException(key, {"0", 0});
        if (std::get<0>(value) == "1")
        {
            features.set(key);
        }
    }
    m_ledgerConfig->setFeatures(features);
}

void LedgerConfigFetcher::fetchCandidateSealerList()
{
    if (m_ledgerConfig->compatibilityVersion() < protocol::BlockVersion::V3_5_VERSION)
    {
        return;
    }
    auto sealerList = fetchNodeListByNodeType(CONSENSUS_CANDIDATE_SEALER);
    TOOL_LOG(INFO) << LOG_DESC("fetchCandidateSealerList success")
                   << LOG_KV("size", sealerList->size());
    m_ledgerConfig->setCandidateSealerNodeList(*sealerList);
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
                          << LOG_KV("code", error->errorCode())
                          << LOG_KV("msg", error->errorMessage())
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
    if (versionStr.empty())
    {
        m_ledgerConfig->setCompatibilityVersion((uint32_t)(bcos::protocol::DEFAULT_VERSION));
        TOOL_LOG(INFO) << LOG_DESC("fetchCompatibilityVersion: empty version, use " +
                                   bcos::protocol::DEFAULT_VERSION_STR + " as default version.");
        return;
    }
    auto version = toVersionNumber(versionStr);
    m_ledgerConfig->setCompatibilityVersion(version);
    TOOL_LOG(INFO) << LOG_DESC("fetchCompatibilityVersion success") << LOG_KV("version", versionStr)
                   << LOG_KV("versionNumber", version)
                   << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion())
                   << LOG_KV("maxSupportedVersion", g_BCOSConfig.maxSupportedVersion());
}

void LedgerConfigFetcher::fetchAuthCheckStatus()
{
    if (versionCompareTo(m_ledgerConfig->compatibilityVersion(), BlockVersion::V3_3_VERSION) < 0)
    {
        return;
    }
    try
    {
        auto ret = fetchSystemConfig(SYSTEM_KEY_AUTH_CHECK_STATUS);
        TOOL_LOG(INFO) << LOG_DESC("fetchAuthCheckStatus success") << LOG_KV("value", ret);
        m_ledgerConfig->setAuthCheckStatus(boost::lexical_cast<uint32_t>(ret));
    }
    catch (...)
    {
        TOOL_LOG(INFO) << LOG_DESC("fetchAuthCheckStatus failed, set default value UINT32_MAX.");
        m_ledgerConfig->setAuthCheckStatus(UINT32_MAX);
    }
}

void LedgerConfigFetcher::fetchEpochBlockNum()
{
    if (m_ledgerConfig->compatibilityVersion() < BlockVersion::V3_5_VERSION)
    {
        return;
    }
    auto ret = fetchSystemConfigNoException(ledger::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, {"1000", 0});
    TOOL_LOG(INFO) << LOG_DESC("fetchEpochBlockNum success") << LOG_KV("value", std::get<0>(ret));
    m_ledgerConfig->setEpochBlockNum({boost::lexical_cast<uint64_t>(std::get<0>(ret)),
        boost::lexical_cast<uint64_t>(std::get<1>(ret))});
}

void LedgerConfigFetcher::fetchEpochSealerNum()
{
    if (m_ledgerConfig->compatibilityVersion() < BlockVersion::V3_5_VERSION)
    {
        return;
    }
    auto ret = fetchSystemConfigNoException(ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, {"4", 0});
    TOOL_LOG(INFO) << LOG_DESC("fetchEpochSealerNum success") << LOG_KV("value", std::get<0>(ret));
    m_ledgerConfig->setEpochSealerNum({boost::lexical_cast<uint64_t>(std::get<0>(ret)),
        boost::lexical_cast<uint64_t>(std::get<1>(ret))});
}

void LedgerConfigFetcher::fetchNotifyRotateFlagInfo()
{
    if (m_ledgerConfig->compatibilityVersion() < BlockVersion::V3_5_VERSION)
    {
        return;
    }
    auto ret = fetchSystemConfigNoException(ledger::INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, {"0", 0});
    TOOL_LOG(INFO) << LOG_DESC("fetchNotifyRotateFlagInfo success")
                   << LOG_KV("value", std::get<0>(ret));
    m_ledgerConfig->setNotifyRotateFlagInfo(boost::lexical_cast<uint64_t>(std::get<0>(ret)));
}

void LedgerConfigFetcher::fetchChainId()
{
    auto [chainIdStr, _] = fetchSystemConfigNoException(SYSTEM_KEY_WEB3_CHAIN_ID, {"0", 0});
    try
    {
        auto chainId = boost::lexical_cast<u256>(chainIdStr);
        TOOL_LOG(INFO) << LOG_DESC("fetchChainId success") << LOG_KV("chainId", chainIdStr);
        m_ledgerConfig->setChainId(bcos::toEvmC(chainId));
    }
    catch (...)
    {
        TOOL_LOG(TRACE) << LOG_DESC("fetchChainId failed") << LOG_KV("chainId", chainIdStr);
        evmc_uint256be chainId{};
        m_ledgerConfig->setChainId(std::move(chainId));
    }
}