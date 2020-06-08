/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : implementation of VRF based rPBFTEngine
 * @file: VRFBasedrPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2020-06-04
 */
#include "VRFBasedrPBFTEngine.h"

using namespace dev::consensus;

void VRFBasedrPBFTEngine::setShouldRotateSealers(bool _shouldRotateSealers)
{
    m_shouldRotateSealers.store(_shouldRotateSealers);
}

// Working sealer is elected as leader in turn
IDXTYPE VRFBasedrPBFTEngine::selectLeader() const
{
    return (IDXTYPE)((m_view + m_highestBlock.number()) % m_epochSize);
}

void VRFBasedrPBFTEngine::updateConsensusNodeList()
{
    ConsensusEngineBase::updateConsensusNodeList();
    // update chosedConsensusNode and x_chosedSealerList
    auto workingSealers = m_blockChain->workingSealerList();
    std::sort(workingSealers.begin(), workingSealers.end());

    UpgradableGuard l(x_chosedSealerList);
    bool chosedSealerListUpdated = false;
    if (workingSealers != *m_chosedSealerList)
    {
        chosedSealerListUpdated = true;
        VRFRPBFTEngine_LOG(INFO) << LOG_DESC("updateConsensusNodeList: update working sealers");
        UpgradeGuard ul(l);
        *m_chosedSealerList = workingSealers;
    }
    // update m_chosedConsensusNodes
    if (chosedSealerListUpdated)
    {
        WriteGuard l(x_chosedConsensusNodes);
        m_chosedConsensusNodes->clear();
        for (auto const& _node : *m_chosedSealerList)
        {
            m_chosedConsensusNodes->insert(_node);
        }
    }
    // the working sealers or the sealers have been updated
    if (chosedSealerListUpdated || m_sealerListUpdated)
    {
        // update consensusInfo when send block status by tree-topology
        updateConsensusInfo();
    }
}

void VRFBasedrPBFTEngine::resetConfig()
{
    PBFTEngine::resetConfig();
    // update the epochBlockNum
    m_rotatingIntervalUpdated = updateRotatingInterval();

    // update m_shouldRotateSealers
    if (0 == (m_blockChain->number() - m_rotatingIntervalEnableNumber) % m_rotatingInterval)
    {
        m_shouldRotateSealers.store(true);
    }
    // update m_epochSize
    int64_t epochSize = m_chosedSealerList->size();
    if (epochSize != m_epochSize.load())
    {
        m_epochSize = epochSize;
        m_f = (epochSize - 1) / 3;
    }
}

void VRFBasedrPBFTEngine::updateConsensusInfo()
{
    // update consensus node info for syncing block
    if (m_blockSync->syncTreeRouterEnabled())
    {
        // get all the node list of this group
        auto nodeList = m_blockChain->sealerList() + m_blockChain->observerList();
        std::sort(nodeList.begin(), nodeList.end());
        m_blockSync->updateConsensusNodeInfo(*m_chosedSealerList, nodeList);
    }
    // update consensus node info for broadcasting transactions
    if (m_treeRouter)
    {
        m_treeRouter->updateConsensusNodeInfo(*m_chosedSealerList);
    }
    VRFRPBFTEngine_LOG(INFO) << LOG_DESC("updateConsensusInfo")
                             << LOG_KV("chosedSealerListSize", m_chosedSealerList->size());
}