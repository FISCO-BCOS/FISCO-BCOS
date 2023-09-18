/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RPBFTConfigTools.h
 * @author: kyonGuo
 * @date 2023/7/14
 */

#pragma once
#include "bcos-framework/consensus/ConsensusInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"

namespace bcos::consensus
{

class RPBFTConfigTools
{
public:
    using Ptr = std::shared_ptr<RPBFTConfigTools>;

    RPBFTConfigTools() : m_candidateNodeList({}), m_consensusNodeList({}) {}

    void resetConfig(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void updateWorkingSealerNodeList(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void updateShouldRotateSealers(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void updateEpochBlockNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);
    [[nodiscard]] bool updateEpochSealerNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);
    void updateNotifyRotateFlag(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void setShouldRotateSealers(bool _shouldRotateSealers);
    bool shouldRotateSealers(protocol::BlockNumber) const;

private:
    // the node index in working consensus node list
    std::atomic<IndexType> m_nodeIndexInWorkingSealer{0};
    // the number of working consensus nodes
    std::atomic_uint64_t m_workingSealerNodeNum{0};

    // working sealer list
    ConsensusNodeList m_candidateNodeList;
    // candidate sealer list
    ConsensusNodeList m_consensusNodeList;
    std::atomic_bool m_workingSealerNodeListUpdated{false};
    std::atomic_bool m_shouldRotateWorkingSealer{false};

    // epoch block num
    std::atomic_uint64_t m_epochBlockNum{0};
    bcos::protocol::BlockNumber m_epochBlockNumEnableNumber{0};

    // epoch consensus node size
    std::atomic_uint64_t m_epochSealerNum{0};
    bcos::protocol::BlockNumber m_epochSealerNumEnableNumber{0};

    std::atomic_uint64_t m_notifyRotateFlag{0};
};

}  // namespace bcos::consensus
