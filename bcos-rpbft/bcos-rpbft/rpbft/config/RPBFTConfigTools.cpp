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
 * @file RPBFTConfigTools.cpp
 * @author: kyonGuo
 * @date 2023/7/16
 */

#include "RPBFTConfigTools.h"
#include "../utilities/Common.h"
#include "bcos-pbft/core/ConsensusConfig.h"

using namespace bcos;
using namespace bcos::consensus;

void RPBFTConfigTools::resetConfig(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    if (!_ledgerConfig->features().get(ledger::Features::Flag::feature_rpbft)) [[likely]]
    {
        return;
    }
    updateWorkingSealerNodeList(_ledgerConfig);
    updateShouldRotateSealers(_ledgerConfig);
}

void RPBFTConfigTools::updateWorkingSealerNodeList(
    const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    m_candidateNodeList = _ledgerConfig->candidateSealerNodeList();

    auto workingSealerNodeList = _ledgerConfig->consensusNodeList();
    // make sure the size of working consensus is non-empty
    // in some scenarios, the number of working consensus will be 0,
    // such as the scenario where the last working consensus is added as consensus
    if (workingSealerNodeList.empty())
    {
        workingSealerNodeList.emplace_back((m_candidateNodeList)[0]);
    }

    std::sort(
        workingSealerNodeList.begin(), workingSealerNodeList.end(), ConsensusNodeComparator());
    // update the consensus list
    // if consensus node list have not been changed
    if (consensus::ConsensusConfig::compareConsensusNode(
            workingSealerNodeList, m_consensusNodeList))
    {
        m_workingSealerNodeListUpdated = false;
        return;
    }
    // consensus node list have been changed
    m_consensusNodeList = workingSealerNodeList;
    m_workingSealerNodeNum = m_consensusNodeList.size();
    m_workingSealerNodeListUpdated = true;

    RPBFT_LOG(INFO) << METRIC << LOG_DESC("updateWorkingConsensusNodeList")
                    << LOG_KV("workingNodeNum", m_workingSealerNodeNum)
                    << LOG_KV("nodeIndexInWorkingSealer", m_nodeIndexInWorkingSealer)
                    << decsConsensusNodeList(workingSealerNodeList);
    //
    //    if (!compareConsensusNode(*m_workingSealerNodeList, *m_consensusNodeList))
    //    {
    //        bcos::consensus::ConsensusNodeList pendingConsensusNodeList;
    //        for (const auto& node : _ledgerConfig->mutableConsensusNodeList())
    //        {
    //            if (!ConsensusConfig::isNodeExist(node, *m_workingSealerNodeList))
    //            {
    //                pendingConsensusNodeList.emplace_back(node);
    //            }
    //        }
    //
    //        *_ledgerConfig->mutableConsensusList() = *m_workingSealerNodeList;
    //        *_ledgerConfig->mutableObserverList() += pendingConsensusNodeList;
    //    }
}

void RPBFTConfigTools::updateShouldRotateSealers(
    const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    // reset shouldRotateSealers
    if (shouldRotateSealers(-1))
    {
        setShouldRotateSealers(false);
    }

    // update according to epoch_block_num
    updateEpochBlockNum(_ledgerConfig);

    // update according to epoch_sealer_num
    bool isEpochSealerNumChanged = updateEpochSealerNum(_ledgerConfig);

    // first setup
    if (_ledgerConfig->blockNumber() == 0)
    {
        return;
    }

    // The previous leader forged an illegal rotating-tx, resulting in a failed
    // verification and the INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE flag was set to true, the current
    // leader needs to rotate working consensus
    updateNotifyRotateFlag(_ledgerConfig);
    if (m_notifyRotateFlag == 1)
    {
        setShouldRotateSealers(true);
        RPBFT_LOG(INFO) << LOG_DESC(
            "resetConfig: the previous leader forged an illegal transaction-rotating,"
            "still rotate working consensus in this epoch");
    }

    // the number of working consensus is smaller than epochSize,
    // trigger rotate in case of the number of working consensus has been decreased to 0
    std::uint64_t maxWorkingSealerNum = std::min(m_epochSealerNum.load(),
        static_cast<uint64_t>(m_consensusNodeList.size() + m_candidateNodeList.size()));
    if (static_cast<std::uint64_t>(m_workingSealerNodeNum) < maxWorkingSealerNum)
    {
        setShouldRotateSealers(true);
    }

    // After the last batch of working consensus agreed on m_rotateInterval,
    // set m_shouldRotateConsensus to true to notify create rotate transaction
    if (isEpochSealerNumChanged ||
        0 == (_ledgerConfig->blockNumber() + 1 - m_epochBlockNumEnableNumber) % m_epochBlockNum)
    {
        setShouldRotateSealers(true);
    }
    RPBFT_LOG(INFO) << LOG_DESC("resetConfig") << LOG_KV("blkNum", _ledgerConfig->blockNumber())
                    << LOG_KV("shouldRotateConsensus", shouldRotateSealers(-1))
                    << LOG_KV("workingConsensusNum", m_workingSealerNodeNum)
                    << LOG_KV("rotateInterval", m_epochBlockNum)
                    << LOG_KV("configuredEpochSealerNum", m_epochSealerNum);
}

void RPBFTConfigTools::updateEpochBlockNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    m_epochBlockNum = std::get<0>(_ledgerConfig->epochBlockNum());
    m_epochBlockNumEnableNumber = std::get<1>(_ledgerConfig->epochBlockNum());
}

bool RPBFTConfigTools::updateEpochSealerNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    if (std::get<0>(_ledgerConfig->epochSealerNum()) == m_epochSealerNum)
    {
        return false;
    }

    std::uint64_t originEpochSealerNum = m_epochSealerNum;
    m_epochSealerNum = std::min((size_t)std::get<0>(_ledgerConfig->epochSealerNum()),
        (m_consensusNodeList.size() + m_candidateNodeList.size()));
    m_epochSealerNumEnableNumber = std::get<1>(_ledgerConfig->epochSealerNum());

    return originEpochSealerNum != m_epochSealerNum;
}

void RPBFTConfigTools::updateNotifyRotateFlag(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig)
{
    m_notifyRotateFlag = _ledgerConfig->notifyRotateFlagInfo();
}

bool RPBFTConfigTools::shouldRotateSealers(protocol::BlockNumber _number) const
{
    if (!m_shouldRotateWorkingSealer && _number != -1)
    {
        if (m_epochBlockNum == 0)
        {
            return false;
        }
        if ((_number - m_epochBlockNumEnableNumber) % m_epochBlockNum == 0)
        {
            return true;
        }
    }
    return m_shouldRotateWorkingSealer;
}

void RPBFTConfigTools::setShouldRotateSealers(bool _shouldRotateSealers)
{
    m_shouldRotateWorkingSealer = _shouldRotateSealers;
}