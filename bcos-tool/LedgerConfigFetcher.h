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
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include <bcos-framework/Common.h>

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

    virtual void fetchBlockNumberAndHash();
    virtual void fetchConsensusNodeList();
    virtual void fetchObserverNodeList();
    virtual void fetchBlockTxCountLimit();
    virtual void fetchGenesisHash();
    virtual void fetchNonceList(protocol::BlockNumber _startNumber, int64_t _offset);
    virtual void fetchConsensusLeaderPeriod();

    // consensus_leader_period
    virtual bcos::ledger::LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }
    virtual std::shared_ptr<std::map<int64_t, bcos::protocol::NonceListPtr>> nonceList()
    {
        return m_nonceList;
    }
    virtual bcos::crypto::HashType const& genesisHash() const { return m_genesisHash; }

    virtual void fetchCompatibilityVersion();

protected:
    virtual bcos::crypto::HashType fetchBlockHash(bcos::protocol::BlockNumber _blockNumber);
    virtual std::string fetchSystemConfig(std::string_view _key);
    virtual bcos::consensus::ConsensusNodeListPtr fetchNodeListByNodeType(std::string_view _type);

    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::ledger::LedgerConfig::Ptr m_ledgerConfig;
    std::shared_ptr<std::map<int64_t, bcos::protocol::NonceListPtr>> m_nonceList;
    bcos::crypto::HashType m_genesisHash;
};
}  // namespace tool
}  // namespace bcos