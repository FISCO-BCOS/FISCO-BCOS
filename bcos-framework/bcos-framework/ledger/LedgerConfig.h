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
 * @brief
 * @file LedgerConfig.h
 * @author: yujiechen
 * @date 2021-05-06
 */
#pragma once
#include "../consensus/ConsensusNodeInterface.h"
#include "../protocol/ProtocolTypeDef.h"
#include "Features.h"

#include <evmc/evmc.hpp>

namespace bcos::ledger
{

constexpr std::uint64_t DEFAULT_EPOCH_SEALER_NUM{4};
constexpr std::uint64_t DEFAULT_EPOCH_BLOCK_NUM{1000};
constexpr std::uint64_t DEFAULT_INTERNAL_NOTIFY_FLAG{0};

class LedgerConfig
{
public:
    using Ptr = std::shared_ptr<LedgerConfig>;
    LedgerConfig()
      : m_consensusNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>()),
        m_observerNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>()),
        m_candidateSealerNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>())
    {}
    virtual ~LedgerConfig() = default;

    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList)
    {
        *m_consensusNodeList = _consensusNodeList;
    }
    virtual void setObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList)
    {
        *m_observerNodeList = _observerNodeList;
    }
    virtual void setCandidateSealerNodeList(
        bcos::consensus::ConsensusNodeList const& candidateSealerNodeList)
    {
        *m_candidateSealerNodeList = candidateSealerNodeList;
    }
    virtual void setHash(bcos::crypto::HashType const& _hash) { m_hash = _hash; }
    virtual void setBlockNumber(bcos::protocol::BlockNumber _blockNumber)
    {
        m_blockNumber = _blockNumber;
    }
    virtual void setBlockTxCountLimit(uint64_t _blockTxCountLimit)
    {
        m_blockTxCountLimit = _blockTxCountLimit;
    }

    virtual bcos::consensus::ConsensusNodeList const& consensusNodeList() const
    {
        return *m_consensusNodeList;
    }

    virtual bcos::consensus::ConsensusNodeList& mutableConsensusNodeList()
    {
        return *m_consensusNodeList;
    }

    virtual bcos::consensus::ConsensusNodeList const& observerNodeList() const
    {
        return *m_observerNodeList;
    }
    virtual bcos::consensus::ConsensusNodeList const& candidateSealerNodeList() const
    {
        return *m_candidateSealerNodeList;
    }
    bcos::crypto::HashType const& hash() const { return m_hash; }
    bcos::protocol::BlockNumber blockNumber() const { return m_blockNumber; }

    void setConsensusType(const std::string& _consensusType) { m_consensusType = _consensusType; }
    std::string consensusType() const { return m_consensusType; }

    uint64_t blockTxCountLimit() const { return m_blockTxCountLimit; }

    bcos::consensus::ConsensusNodeListPtr mutableConsensusList() { return m_consensusNodeList; }
    bcos::consensus::ConsensusNodeListPtr mutableObserverList() { return m_observerNodeList; }
    bcos::consensus::ConsensusNodeListPtr mutableCandidateSealerNodeList()
    {
        return m_candidateSealerNodeList;
    }

    uint64_t leaderSwitchPeriod() const { return m_leaderSwitchPeriod; }
    void setLeaderSwitchPeriod(uint64_t _leaderSwitchPeriod)
    {
        m_leaderSwitchPeriod = _leaderSwitchPeriod;
    }

    std::tuple<uint64_t, protocol::BlockNumber> const& gasLimit() const { return m_gasLimit; }
    void setGasLimit(std::tuple<uint64_t, protocol::BlockNumber> mGasLimit)
    {
        m_gasLimit = std::move(mGasLimit);
    }

    std::tuple<std::string, protocol::BlockNumber> const& gasPrice() const { return m_gasPrice; }
    void setGasPrice(std::tuple<std::string, protocol::BlockNumber> mGasPrice)
    {
        m_gasPrice = std::move(mGasPrice);
    }

    // Not enforce to set this field, in memory data
    void setSealerId(int64_t _sealerId) { m_sealerId = _sealerId; }
    int64_t sealerId() const { return m_sealerId; }

    // Not enforce to set this field, in memory data
    void setTxsSize(int64_t _txsSize) { m_txsSize = _txsSize; }
    int64_t txsSize() const { return m_txsSize; }

    void setCompatibilityVersion(uint32_t _version) { m_compatibilityVersion = _version; }
    uint32_t compatibilityVersion() const { return m_compatibilityVersion; }

    void setAuthCheckStatus(uint32_t _authStatus) { m_authCheckStatus = _authStatus; }
    uint32_t authCheckStatus() const { return m_authCheckStatus; }

    void setEpochSealerNum(std::tuple<uint64_t, protocol::BlockNumber> _epochSealerNum)
    {
        m_epochSealerNum = _epochSealerNum;
    }
    std::tuple<uint64_t, protocol::BlockNumber> const& epochSealerNum() const
    {
        return m_epochSealerNum;
    }

    void setEpochBlockNum(std::tuple<uint64_t, protocol::BlockNumber> _epochBlockNum)
    {
        m_epochBlockNum = _epochBlockNum;
    }
    std::tuple<uint64_t, protocol::BlockNumber> const& epochBlockNum() const
    {
        return m_epochBlockNum;
    }

    void setNotifyRotateFlagInfo(const uint64_t _notifyRotateFlagInfo)
    {
        m_notifyRotateFlagInfo = _notifyRotateFlagInfo;
    }
    uint64_t notifyRotateFlagInfo() const { return m_notifyRotateFlagInfo; }

    Features const& features() const { return m_features; }
    void setFeatures(Features features) { m_features = features; }

    std::optional<evmc_uint256be> const& chainId() const { return m_chainId; }
    void setChainId(evmc_uint256be _chainId) { m_chainId = std::move(_chainId); }

private:
    bcos::consensus::ConsensusNodeListPtr m_consensusNodeList;
    bcos::consensus::ConsensusNodeListPtr m_observerNodeList;
    bcos::consensus::ConsensusNodeListPtr m_candidateSealerNodeList;
    bcos::crypto::HashType m_hash;
    bcos::protocol::BlockNumber m_blockNumber = 0;
    std::string m_consensusType;
    uint64_t m_blockTxCountLimit = 0;
    uint64_t m_leaderSwitchPeriod = 1;
    std::tuple<uint64_t, protocol::BlockNumber> m_gasLimit = {3000000000, 0};
    std::tuple<std::string, protocol::BlockNumber> m_gasPrice = {"0x0", 0};
    std::tuple<uint64_t, protocol::BlockNumber> m_epochSealerNum = {4, 0};
    std::tuple<uint64_t, protocol::BlockNumber> m_epochBlockNum = {1000, 0};
    uint64_t m_notifyRotateFlagInfo{0};
    // the compatibilityVersion
    // the system version, can only be upgraded manually
    uint32_t m_compatibilityVersion = 0;
    // no need to store, in memory data
    int64_t m_sealerId = -1;
    int64_t m_txsSize = -1;
    uint32_t m_authCheckStatus = 0;
    Features m_features;
    std::optional<evmc_uint256be> m_chainId = {};
};
}  // namespace bcos::ledger
