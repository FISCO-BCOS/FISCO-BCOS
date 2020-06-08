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
 * @brief : implementation of VRF based rPBFTSealer
 * @file: VRFBasedrPBFTSealer.cpp
 * @author: yujiechen
 * @date: 2020-06-04
 */
#include "VRFBasedrPBFTSealer.h"
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>

using namespace dev::consensus;

VRFBasedrPBFTSealer::VRFBasedrPBFTSealer(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
    std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
    std::shared_ptr<dev::sync::SyncInterface> _blockSync)
  : PBFTSealer(_txPool, _blockChain, _blockSync)
{}

// reset the thread name for VRFBasedrPBFTSealer
void VRFBasedrPBFTSealer::initConsensusEngine()
{
    PBFTSealer::initConsensusEngine();
    std::string threadName = "rPBFTSeal-" + std::to_string(m_pbftEngine->groupId());
    setThreadName(threadName);
    // convert PBFTEngine into VRFBasedrPBFTEngine
    m_vrfBasedrPBFTEngine = std::dynamic_pointer_cast<VRFBasedrPBFTEngine>(m_pbftEngine);
    // create txGenerator
    m_txGenerator = std::make_shared<TxGenerator>(
        m_pbftEngine->groupId(), g_BCOSConfig.chainId(), m_txPool->maxBlockLimit() / 2);

    // TODO: Initialize VRF public key from the private key
    VRFRPBFTSealer_LOG(INFO) << LOG_DESC("initConsensusEngine");
}

bool VRFBasedrPBFTSealer::hookBeforeSealBlock()
{
    if (!m_vrfBasedrPBFTEngine->shouldRotateSealers())
    {
        return true;
    }
    // generate transaction exceptioned
    if (!generateAndSealerRotatingTx())
    {
        return false;
    }
    m_vrfBasedrPBFTEngine->setShouldRotateSealers(false);
    return true;
}

// TODO: call rust VRF library to generate VRF proof
std::string VRFBasedrPBFTSealer::generateVRFProof(std::string const& _inputData)
{
    return _inputData;
}

bool VRFBasedrPBFTSealer::generateAndSealerRotatingTx()
{
    try
    {
        // already has a transaction
        auto txsSize = m_sealing.block->getTransactionSize();
        if (txsSize >= 1)
        {
            // Note: this situation should be strictly avoided
            VRFRPBFTSealer_LOG(WARNING)
                << LOG_DESC("generateAndSealerRotatingTx failed for txs already exists")
                << LOG_KV("txSize", txsSize);
        }
        // clear the loaded transaction
        m_sealing.block->transactions()->clear();
        // get VRF proof
        auto blockNumber = m_blockChain->number();
        auto blockHash = m_blockChain->numberHash(blockNumber);
        auto blockHashStr = toHex(blockHash);
        auto vrfProof = generateVRFProof(blockHashStr);

        auto const& nodeRotatingInfo = m_vrfBasedrPBFTEngine->nodeRotatingInfo();

        // generate "rotateWorkingSealer" transaction
        std::string interface = dev::precompiled::WSM_METHOD_ROTATE_STR;
        auto generatedTx = m_txGenerator->generateTransactionWithSig(interface,
            m_blockChain->number(), dev::precompiled::WORKING_SEALER_MGR_ADDRESS,
            m_vrfBasedrPBFTEngine->keyPair(), dev::eth::Transaction::Type::MessageCall,
            m_vrfPublicKey, blockHashStr, vrfProof, nodeRotatingInfo.removedWorkingSealerNum,
            nodeRotatingInfo.insertedWorkingSealerNum);

        // put the generated transaction into the 0th position of the block transactions
        m_sealing.block->transactions()->push_back(generatedTx);

        VRFRPBFTSealer_LOG(DEBUG) << LOG_DESC("generateAndSealerRotatingTx succ")
                                  << LOG_KV("nodeIdx", m_vrfBasedrPBFTEngine->nodeIdx())
                                  << LOG_KV("blkNum", blockNumber)
                                  << LOG_KV("hash", blockHash.abridged())
                                  << LOG_KV("nodeId",
                                         m_vrfBasedrPBFTEngine->keyPair().pub().abridged());
    }
    catch (const std::exception& _e)
    {
        VRFRPBFTSealer_LOG(ERROR) << LOG_DESC("generateAndSealerRotatingTx failed")
                                  << LOG_KV("reason", boost::diagnostic_information(_e));
        return false;
    }
    return true;
}