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
 * @file VRFBasedSealer.cpp
 * @author: kyonGuo
 * @date 2023/7/5
 */

#include "VRFBasedSealer.h"
#include "bcos-pbft/core/ConsensusConfig.h"
#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-txpool/TxPool.h>
#include <wedpr-crypto/WedprCrypto.h>

namespace bcos::sealer
{

bool VRFBasedSealer::hookWhenSealBlock(bcos::protocol::Block::Ptr _block)
{
    const auto& consensusConfig = dynamic_cast<consensus::ConsensusConfig const&>(
        *m_sealerConfig->consensus()->consensusConfig());
    if (!consensusConfig.shouldRotateSealers())
    {
        return true;
    }
    return generateTransactionForRotating(_block);
}

bool VRFBasedSealer::generateTransactionForRotating(bcos::protocol::Block::Ptr& _block)
{
    try
    {
        auto blockNumber = m_sealingManager->currentNumber();
        auto blockHash = m_sealingManager->currentHash();
        auto keyPair = m_sealerConfig->keyPair();
        CInputBuffer privateKey{reinterpret_cast<const char*>(keyPair->secretKey()->data().data()),
            keyPair->secretKey()->size()};
        CInputBuffer inputMsg{reinterpret_cast<const char*>(blockHash.data()), blockHash.size()};
        std::string vrfProof;
        // FIXME: make it constant
        size_t proofSize = 96;
        vrfProof.resize(proofSize);
        COutputBuffer proof{vrfProof.data(), proofSize};
        auto ret = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
        if (ret != WEDPR_SUCCESS)
        {
            SEAL_LOG(WARNING) << LOG_DESC(
                                     "generateTransactionForRotating: generate vrf-proof failed")
                              << LOG_KV("inputData", blockHash.hex());
            return false;
        }

        std::string interface = precompiled::WSM_METHOD_ROTATE_STR;

        bcos::CodecWrapper codec(m_hashImpl, g_BCOSConfig.isWasm());
        auto input =
            codec.encodeWithSig(interface, keyPair->publicKey()->hex(), blockHash.hex(), vrfProof);

        auto tx = m_sealerConfig->blockFactory()->transactionFactory()->createTransaction(0,
            precompiled::CONSENSUS_ADDRESS, input, std::to_string(utcSteadyTimeUs()), INT64_MAX,
            m_sealerConfig->chainId(), m_sealerConfig->groupId(), utcTime(), keyPair);

        // append in txpool, in case other peers need it
        // FIXME: too tricky hear
        auto& txpool = dynamic_cast<txpool::TxPool&>(*m_sealerConfig->txpool());
        txpool.txpoolStorage()->insert(tx);

        // put the generated transaction into the 0th position of the block transactions
        // Note: must set generatedTx into the first transaction for other transactions may change
        //       the _sys_config_ and _sys_consensus_
        //       here must use noteChange for this function will notify updating the txsCache
        auto txMeta = m_sealerConfig->blockFactory()->createTransactionMetaData(
            tx->hash(), std::string(tx->to()));
        _block->appendTransactionMetaData(txMeta);

        if (c_fileLogLevel <= DEBUG)
        {
            SEAL_LOG(DEBUG) << LOG_DESC("generateTransactionForRotating succ")
                            << LOG_KV("nodeIdx", m_sealerConfig->consensus()->nodeIndex())
                            << LOG_KV("blkNum", blockNumber) << LOG_KV("hash", blockHash.abridged())
                            << LOG_KV("nodeId", keyPair->publicKey()->hex());
        }
    }
    catch (const std::exception& e)
    {
        SEAL_LOG(ERROR) << LOG_DESC("generateTransactionForRotating failed")
                        << LOG_KV("nodeIdx", m_sealerConfig->consensus()->nodeIndex())
                        << LOG_KV("exception", boost::diagnostic_information(e));
        return false;
    }
    return true;
}
}  // namespace bcos::sealer
