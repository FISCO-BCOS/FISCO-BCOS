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
#include "../../interfaces/consensus/ConsensusNodeInterface.h"
#include "../../interfaces/protocol/ProtocolTypeDef.h"
namespace bcos
{
namespace ledger
{
class LedgerConfig
{
public:
    using Ptr = std::shared_ptr<LedgerConfig>;
    LedgerConfig()
      : m_consensusNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>()),
        m_observerNodeList(std::make_shared<bcos::consensus::ConsensusNodeList>())
    {}
    virtual ~LedgerConfig() {}

    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList)
    {
        *m_consensusNodeList = _consensusNodeList;
    }
    virtual void setObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList)
    {
        *m_observerNodeList = _observerNodeList;
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
    bcos::crypto::HashType const& hash() const { return m_hash; }
    bcos::protocol::BlockNumber blockNumber() const { return m_blockNumber; }

    uint64_t blockTxCountLimit() const { return m_blockTxCountLimit; }

    bcos::consensus::ConsensusNodeListPtr mutableConsensusList() { return m_consensusNodeList; }
    bcos::consensus::ConsensusNodeListPtr mutableObserverList() { return m_observerNodeList; }

    uint64_t leaderSwitchPeriod() const { return m_leaderSwitchPeriod; }
    void setLeaderSwitchPeriod(uint64_t _leaderSwitchPeriod)
    {
        m_leaderSwitchPeriod = _leaderSwitchPeriod;
    }

    uint64_t gasLimit() const { return m_gasLimit; }
    void setGasLimit(uint64_t mGasLimit) { m_gasLimit = mGasLimit; }

    // Not enforce to set this field, in memory data
    void setSealerId(int64_t _sealerId) { m_sealerId = _sealerId; }
    int64_t sealerId() const { return m_sealerId; }

    // Not enforce to set this field, in memory data
    void setTxsSize(int64_t _txsSize) { m_txsSize = _txsSize; }
    int64_t txsSize() const { return m_txsSize; }

protected:
    bcos::consensus::ConsensusNodeListPtr m_consensusNodeList;
    bcos::consensus::ConsensusNodeListPtr m_observerNodeList;
    bcos::crypto::HashType m_hash;
    bcos::protocol::BlockNumber m_blockNumber;
    uint64_t m_blockTxCountLimit;
    uint64_t m_leaderSwitchPeriod = 1;
    uint64_t m_gasLimit = 300000000;
    // no need to store, in memory data
    int64_t m_sealerId = -1;
    int64_t m_txsSize = -1;
};
}  // namespace ledger
}  // namespace bcos