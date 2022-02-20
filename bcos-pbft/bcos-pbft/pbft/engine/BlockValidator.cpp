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
 * @brief validator for the block from the sync module
 * @file BlockValidator.cpp
 * @author: yujiechen
 * @date 2021-05-25
 */
#include "BlockValidator.h"
#include "../utilities/Common.h"
using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;

// check the block
void BlockValidator::asyncCheckBlock(
    Block::Ptr _block, std::function<void(Error::Ptr, bool)> _onVerifyFinish)
{
    auto self = std::weak_ptr<BlockValidator>(shared_from_this());
    m_taskPool->enqueue([self, _block, _onVerifyFinish]() {
        auto blockHeader = _block->blockHeader();
        // ignore the genesis block
        if (blockHeader->number() == 0)
        {
            _onVerifyFinish(nullptr, true);
            return;
        }
        try
        {
            auto validator = self.lock();
            if (!validator)
            {
                _onVerifyFinish(nullptr, false);
                return;
            }
            auto config = validator->m_config;
            if (blockHeader->number() <= config->committedProposal()->index())
            {
                _onVerifyFinish(nullptr, false);
                return;
            }
            if (_block->transactionsSize() == 0)
            {
                _onVerifyFinish(nullptr, true);
                return;
            }
            if (!validator->checkSealerListAndWeightList(_block))
            {
                _onVerifyFinish(nullptr, false);
                return;
            }
            if (!validator->checkSignatureList(_block))
            {
                _onVerifyFinish(nullptr, false);
                return;
            }
            _onVerifyFinish(nullptr, true);
        }
        catch (std::exception const& e)
        {
            PBFT_LOG(WARNING) << LOG_DESC("asyncCheckBlock exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
            _onVerifyFinish(nullptr, false);
        }
    });
}

bool BlockValidator::checkSealerListAndWeightList(Block::Ptr _block)
{
    // Note: for tars service, blockHeader must be here to ensure the sealer list not been released
    auto blockHeader = _block->blockHeader();
    // check sealer(sealer must be a consensus node)
    if (!m_config->getConsensusNodeByIndex(blockHeader->sealer()))
    {
        PBFT_LOG(ERROR)
            << LOG_DESC("checkBlock for sync module: invalid sealer for not a consensus node")
            << LOG_KV("sealer", blockHeader->sealer())
            << LOG_KV("hash", blockHeader->hash().abridged())
            << LOG_KV("number", blockHeader->number());
        return false;
    }
    // check the sealer list
    auto blockSealerList = blockHeader->sealerList();
    auto blockWeightList = blockHeader->consensusWeights();
    auto consensusNodeList = m_config->consensusNodeList();
    if ((size_t)blockSealerList.size() != (size_t)consensusNodeList.size() ||
        (size_t)blockWeightList.size() != (size_t)consensusNodeList.size())
    {
        PBFT_LOG(ERROR) << LOG_DESC("checkBlock for sync module: wrong sealerList")
                        << LOG_KV("Nsealer", consensusNodeList.size())
                        << LOG_KV("NBlockSealer", blockSealerList.size())
                        << LOG_KV("NBlockWeight", blockWeightList.size())
                        << LOG_KV("hash", blockHeader->hash().abridged())
                        << LOG_KV("number", blockHeader->number()) << m_config->printCurrentState();
        return false;
    }
    size_t i = 0;
    for (auto const& blockSealer : blockSealerList)
    {
        auto consNodePtr = consensusNodeList[i];
        if (consNodePtr->nodeID()->data() != blockSealer)
        {
            PBFT_LOG(ERROR) << LOG_DESC("checkBlock for sync module: inconsistent sealerList")
                            << LOG_KV("blkSealer", consNodePtr->nodeID()->shortHex())
                            << LOG_KV("chainSealer", *toHexString(blockSealer))
                            << LOG_KV("number", blockHeader->number())
                            << LOG_KV("weight", consNodePtr->weight())
                            << m_config->printCurrentState();
            return false;
        }
        // check weight
        auto blockWeight = blockWeightList[i];
        if (consNodePtr->weight() != blockWeight)
        {
            PBFT_LOG(ERROR) << LOG_DESC("checkBlock for sync module: inconsistent weight")
                            << LOG_KV("blkWeight", blockWeight)
                            << LOG_KV("chainWeight", consNodePtr->weight())
                            << LOG_KV("number", blockHeader->number())
                            << LOG_KV("blkSealer", consNodePtr->nodeID()->shortHex())
                            << LOG_KV("chainSealer", *toHexString(blockSealer))
                            << m_config->printCurrentState();
            return false;
        }
        i++;
    }
    return true;
}

bool BlockValidator::checkSignatureList(Block::Ptr _block)
{
    // check sign num
    // Note: for tars service, blockHeader must be here to ensure the signatureList
    auto blockHeader = _block->blockHeader();
    auto signatureList = blockHeader->signatureList();
    // check sign and weight
    size_t signatureWeight = 0;
    for (auto const& sign : signatureList)
    {
        auto nodeIndex = sign.index;
        auto nodeInfo = m_config->getConsensusNodeByIndex(nodeIndex);
        auto signatureData = ref(sign.signature);
        if (!signatureData.data())
        {
            PBFT_LOG(FATAL) << LOG_DESC("BlockValidator checkSignatureList: invalid signature")
                            << LOG_KV("signatureSize", signatureList.size())
                            << LOG_KV("nodeIndex", nodeIndex)
                            << LOG_KV("number", blockHeader->number())
                            << LOG_KV("hash", blockHeader->hash().abridged());
        }
        if (!nodeInfo || !m_config->cryptoSuite()->signatureImpl()->verify(
                             nodeInfo->nodeID(), blockHeader->hash(), signatureData))
        {
            PBFT_LOG(ERROR) << LOG_DESC("checkBlock for sync module: checkSign failed")
                            << LOG_KV("sealerIdx", nodeIndex)
                            << LOG_KV("blockHash", blockHeader->hash().abridged())
                            << LOG_KV("number", blockHeader->number());
            return false;
        }
        signatureWeight += nodeInfo->weight();
    }
    if (signatureWeight < (size_t)m_config->minRequiredQuorum())
    {
        PBFT_LOG(ERROR) << LOG_DESC("checkBlock for sync module: insufficient signatures")
                        << LOG_KV("signNum", signatureList.size())
                        << LOG_KV("sigWeight", signatureWeight)
                        << LOG_KV("minRequiredQuorum", m_config->minRequiredQuorum());
        return false;
    }
    return true;
}