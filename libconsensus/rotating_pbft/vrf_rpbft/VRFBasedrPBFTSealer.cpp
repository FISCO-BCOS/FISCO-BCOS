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
#include "Common.h"
#include "ffi_vrf.h"
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>

using namespace bcos::consensus;

VRFBasedrPBFTSealer::VRFBasedrPBFTSealer(std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
    std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
    std::shared_ptr<bcos::sync::SyncInterface> _blockSync)
  : PBFTSealer(_txPool, _blockChain, _blockSync)
{}

// reset the thread name for VRFBasedrPBFTSealer
void VRFBasedrPBFTSealer::initConsensusEngine()
{
    PBFTSealer::initConsensusEngine();
    std::string threadName = "rPBFTSeal-" + std::to_string(m_pbftEngine->groupId());
    setName(threadName);
    // convert PBFTEngine into VRFBasedrPBFTEngine
    m_vrfBasedrPBFTEngine = std::dynamic_pointer_cast<VRFBasedrPBFTEngine>(m_pbftEngine);
    // create txGenerator
    m_txGenerator = std::make_shared<TxGenerator>(
        m_pbftEngine->groupId(), g_BCOSConfig.chainId(), m_txPool->maxBlockLimit() / 2);

    // get private key and generate vrf-public-key
    m_privateKey =
        toHex(bytesConstRef{m_vrfBasedrPBFTEngine->keyPair().secret().data(), c_privateKeyLen});
    auto vrfPublicKey = curve25519_vrf_generate_key_pair(m_privateKey.c_str());
    if (vrfPublicKey == nullptr)
    {
        VRFRPBFTSealer_LOG(ERROR) << LOG_DESC(
            "initConsensusEngine failed for the failure to initialize the vrf public key");

        BOOST_THROW_EXCEPTION(
            InitVRFPublicKeyFailed() << errinfo_comment(
                "nitConsensusEngine failed for the failure to initialize the vrf public key"));
    }
    m_vrfPublicKey = vrfPublicKey;
    VRFRPBFTSealer_LOG(INFO) << LOG_DESC("initConsensusEngine") << LOG_KV("vrfPk", m_vrfPublicKey);
}

bool VRFBasedrPBFTSealer::hookAfterHandleBlock()
{
    if (!m_vrfBasedrPBFTEngine->shouldRotateSealers())
    {
        return true;
    }
    // generate transaction exceptioned
    if (!generateTransactionForRotating())
    {
        return false;
    }
    return true;
}

bool VRFBasedrPBFTSealer::generateTransactionForRotating()
{
    try
    {
        // get VRF proof
        auto blockNumber = m_blockChain->number();
        auto blockHash = m_blockChain->numberHash(blockNumber);
        auto blockHashStr = toHex(blockHash);
        auto vrfProofPtr = curve25519_vrf_proof(m_privateKey.c_str(), blockHashStr.c_str());
        if (!vrfProofPtr)
        {
            VRFRPBFTSealer_LOG(WARNING)
                << LOG_DESC("generateTransactionForRotating: generate vrf-proof failed")
                << LOG_KV("inputData", blockHashStr);
            return false;
        }
        std::string vrfProof = vrfProofPtr;

        // generate "rotateWorkingSealer" transaction
        std::string interface = bcos::precompiled::WSM_METHOD_ROTATE_STR;
        auto generatedTx =
            m_txGenerator->generateTransactionWithSig(interface, m_blockChain->number(),
                bcos::precompiled::WORKING_SEALER_MGR_ADDRESS, m_vrfBasedrPBFTEngine->keyPair(),
                bcos::eth::Transaction::Type::MessageCall, m_vrfPublicKey, blockHashStr, vrfProof);

        // put the generated transaction into the 0th position of the block transactions
        // Note: must set generatedTx into the first transaction for other transactions may change
        //       the _sys_config_ and _sys_consensus_
        //       here must use noteChange for this function will notify updating the txsCache
        if (m_sealing.block->transactions()->size() > 0)
        {
            (*(m_sealing.block->transactions()))[0] = generatedTx;
            m_sealing.block->noteChange();
        }
        else
        {
            m_sealing.block->appendTransaction(generatedTx);
        }
        VRFRPBFTSealer_LOG(DEBUG) << LOG_DESC("generateTransactionForRotating succ")
                                  << LOG_KV("nodeIdx", m_vrfBasedrPBFTEngine->nodeIdx())
                                  << LOG_KV("blkNum", blockNumber)
                                  << LOG_KV("hash", blockHash.abridged())
                                  << LOG_KV("nodeId",
                                         m_vrfBasedrPBFTEngine->keyPair().pub().abridged());
    }
    catch (const std::exception& _e)
    {
        VRFRPBFTSealer_LOG(ERROR) << LOG_DESC("generateTransactionForRotating failed")
                                  << LOG_KV("reason", boost::diagnostic_information(_e));
        return false;
    }
    return true;
}
