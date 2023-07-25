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
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
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
    return generateTransactionForRotating(_block, m_sealerConfig, m_sealingManager, m_hashImpl);
}

bool VRFBasedSealer::generateTransactionForRotating(bcos::protocol::Block::Ptr& _block,
    SealerConfig::Ptr const& _sealerConfig, SealingManager::ConstPtr const& _sealingManager,
    crypto::Hash::Ptr const& _hashImpl)
{
    try
    {
        auto blockNumber = _block->blockHeader()->number();
        auto blockHash = _sealingManager->currentHash();
        auto keyPair = _sealerConfig->keyPair();
        CInputBuffer privateKey{reinterpret_cast<const char*>(keyPair->secretKey()->data().data()),
            keyPair->secretKey()->size()};
        bytes vrfPublicKey;
        // FIXME: make it constant
        vrfPublicKey.resize(32);
        COutputBuffer publicKey{(char*)vrfPublicKey.data(), vrfPublicKey.size()};
        auto pubkeyDerive = wedpr_curve25519_vrf_derive_public_key(&privateKey, &publicKey);

        CInputBuffer inputMsg{reinterpret_cast<const char*>(blockHash.data()), blockHash.size()};
        bcos::bytes vrfProof;
        // FIXME: make it constant
        size_t proofSize = 96;
        vrfProof.resize(proofSize);
        COutputBuffer proof{(char*)vrfProof.data(), proofSize};
        auto vrfProve = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
        if (vrfProve != WEDPR_SUCCESS || pubkeyDerive != WEDPR_SUCCESS)
        {
            SEAL_LOG(WARNING) << LOG_DESC(
                                     "generateTransactionForRotating: generate vrf-proof failed")
                              << LOG_KV("inputData", blockHash.hex());
            return false;
        }

        std::string interface = precompiled::WSM_METHOD_ROTATE_STR;

        auto random = std::mt19937(std::random_device{}());
        bcos::CodecWrapper codec(_hashImpl, g_BCOSConfig.isWasm());
        auto input = codec.encodeWithSig(interface, vrfPublicKey, blockHash.asBytes(), vrfProof);

        auto tx = _sealerConfig->blockFactory()->transactionFactory()->createTransaction(0,
            g_BCOSConfig.isWasm() ? precompiled::CONSENSUS_TABLE_NAME :
                                    precompiled::CONSENSUS_ADDRESS,
            input, std::to_string(utcSteadyTimeUs() * random()),
            _sealingManager->currentNumber() + 600, _sealerConfig->chainId(),
            _sealerConfig->groupId(), utcTime(), keyPair);

        // append in txpool, in case other peers need it
        // FIXME: too tricky here
        auto& txpool = dynamic_cast<txpool::TxPool&>(*_sealerConfig->txpool());
        auto& txpoolStorage = dynamic_cast<txpool::MemoryStorage&>(*txpool.txpoolStorage());
        auto submitResult = txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, true);
        if (submitResult != protocol::TransactionStatus::None) [[unlikely]]
        {
            SEAL_LOG(WARNING) << LOG_DESC("generateTransactionForRotating failed for txpool submit")
                              << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                              << LOG_KV("submitResult", submitResult);
            return false;
        }


        // put the generated transaction into the 0th position of the block transactions
        // Note: must set generatedTx into the first transaction for other transactions may change
        //       the _sys_config_ and _sys_consensus_
        //       here must use noteChange for this function will notify updating the txsCache
        auto txMeta = _sealerConfig->blockFactory()->createTransactionMetaData(
            tx->hash(), std::string(tx->to()));
        auto address = right160(_hashImpl->hash(keyPair->publicKey()));
        txMeta->setSource(address.hex());
        _block->appendTransactionMetaData(txMeta);

        SEAL_LOG(INFO) << LOG_DESC("generateTransactionForRotating succ")
                       << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                       << LOG_KV("blkNum", blockNumber) << LOG_KV("hash", blockHash.abridged())
                       << LOG_KV("nodeId", keyPair->publicKey()->hex());
    }
    catch (const std::exception& e)
    {
        SEAL_LOG(INFO) << LOG_DESC("generateTransactionForRotating failed")
                       << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                       << LOG_KV("exception", boost::diagnostic_information(e));
        return false;
    }
    return true;
}
}  // namespace bcos::sealer
