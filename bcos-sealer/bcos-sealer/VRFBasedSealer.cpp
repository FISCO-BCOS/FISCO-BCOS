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
#include "Common.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/sealer/VrfCurveType.h"
#include "bcos-pbft/core/ConsensusConfig.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-txpool/TxPool.h>
#include <wedpr-crypto/WedprCrypto.h>
#include <boost/endian/conversion.hpp>
#include <cstdint>

namespace bcos::sealer
{

sealer::VrfCurveType VRFBasedSealer::getVrfCurveType(ledger::Features const& features)
{
    // FIB-160: pure-function curve selection from a caller-owned Features
    // snapshot. The caller is responsible for snapshot consistency.
    if (features.get(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1))
    {
        return sealer::VrfCurveType::SECKP256K1;
    }
    return sealer::VrfCurveType::CURVE25519;
}

sealer::VrfCurveType VRFBasedSealer::getVrfCurveType(SealerConfig::Ptr const& _sealerConfig)
{
    // Legacy entry point. features() now takes a shared_lock (FIB-160), so the
    // single read is internally safe; cross-read consistency with other flags
    // requires the snapshot path.
    return getVrfCurveType(_sealerConfig->consensus()->consensusConfig()->features());
}

uint16_t VRFBasedSealer::hookWhenSealBlock(bcos::protocol::Block::Ptr _block)
{
    // FIB-160: take ONE atomic rotation snapshot (rotate decision + features)
    // at the start of the rotating-tx generation pass. All downstream reads
    // (curve choice, blockNumberInput flag) use this snapshot so a concurrent
    // setFeatures cannot make the rotate decision and the VRF mode disagree.
    auto const& consensusConfig = dynamic_cast<consensus::ConsensusConfig const&>(
        *m_sealerConfig->consensus()->consensusConfig());
    auto snap = consensusConfig.getRotationSnapshot(
        _block == nullptr ? -1 : _block->blockHeader()->number());
    if (!snap.shouldRotateSealers)
    {
        return SealBlockResult::SUCCESS;
    }
    return generateTransactionForRotating(
        _block, m_sealerConfig, m_sealingManager, m_hashImpl, snap.features);
}

uint16_t VRFBasedSealer::generateTransactionForRotating(bcos::protocol::Block::Ptr& _block,
    SealerConfig::Ptr const& _sealerConfig, SealingManager::ConstPtr const& _sealingManager,
    crypto::Hash::Ptr const& _hashImpl, ledger::Features const& features)
{
    // FIB-160 snapshot-aware path: caller has already taken a single
    // RotationSnapshot; we derive curve choice and blockNumberInput flag from
    // that same snapshot so they cannot disagree under concurrent setFeatures.
    bool blockNumberInput =
        features.get(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input);
    try
    {
        auto blockNumber = _block->blockHeader()->number();
        if (!blockNumberInput && _sealingManager->latestNumber() < blockNumber - 1)
        {
            SEAL_LOG(INFO) << LOG_DESC(
                                  "generateTransactionForRotating: interrupt pipeline for waiting "
                                  "latest block commit")
                           << LOG_KV("latestNumber", _sealingManager->latestNumber())
                           << LOG_KV("sealingNumber", blockNumber)
                           << LOG_KV("blockNumberInput", blockNumberInput);
            return SealBlockResult::WAIT_FOR_LATEST_BLOCK;
        }
        auto keyPair = _sealerConfig->keyPair();
        CInputBuffer privateKey{reinterpret_cast<const char*>(keyPair->secretKey()->data().data()),
            keyPair->secretKey()->size()};
        bytes vrfPublicKey;
        bcos::bytes vrfProof;
        // FIB-160: curve choice from the same Features snapshot as blockNumberInput
        // above, so a concurrent setFeatures cannot make the two flags disagree.
        sealer::VrfCurveType vrfCurveType = getVrfCurveType(features);
        int8_t vrfProve = 0;
        int8_t pubkeyDerive = 0;
        auto blockHash = _sealingManager->latestHash();
        auto blockNumberBigEndian = boost::endian::native_to_big(blockNumber);
        CInputBuffer inputMsg = {
            .data = blockNumberInput ?
                        reinterpret_cast<const char*>(std::addressof(blockNumberBigEndian)) :
                        reinterpret_cast<const char*>(blockHash.data()),
            .len = blockNumberInput ? sizeof(blockNumberBigEndian) :
                                      static_cast<size_t>(blockHash.size())};
        if (vrfCurveType == sealer::VrfCurveType::CURVE25519)
        {
            vrfPublicKey.resize(curve25519PublicKeySize);
            COutputBuffer publicKey{(char*)vrfPublicKey.data(), vrfPublicKey.size()};
            // NOTE: curve25519 fits sm2 and secp256k1 private key value range, so if you want to
            // change elliptic curve, do think twice here.
            pubkeyDerive = wedpr_curve25519_vrf_derive_public_key(&privateKey, &publicKey);

            vrfProof.resize(curve25519VRFProofSize);
            COutputBuffer proof{(char*)vrfProof.data(), curve25519VRFProofSize};
            // FIB-147: do NOT redeclare 'vrfProve' here. The previous code used
            // 'auto vrfProve = ...' which shadowed the outer-scope int8_t vrfProve
            // (declared at function scope, initialized to 0). The error check below
            // (vrfProve != WEDPR_SUCCESS) tested the outer's initial 0 (treated as
            // success), so a failed curve25519 VRF proof was silently accepted and
            // the sealer continued with an invalid/empty proof. Assign to the outer
            // variable so the subsequent check sees the actual call result.
            vrfProve = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
        }
        else if (vrfCurveType == sealer::VrfCurveType::SECKP256K1)
        {
            vrfPublicKey.resize(secp256k1PublicKeySize);
            COutputBuffer publicKey{(char*)vrfPublicKey.data(), vrfPublicKey.size()};
            pubkeyDerive = wedpr_secp256k1_vrf_derive_public_key(&privateKey, &publicKey);

            vrfProof.resize(secp256k1VRFProofSize);
            COutputBuffer proof{(char*)vrfProof.data(), secp256k1VRFProofSize};
            vrfProve = wedpr_secp256k1_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
        }


        if (vrfProve != WEDPR_SUCCESS || pubkeyDerive != WEDPR_SUCCESS) [[unlikely]]
        {
            SEAL_LOG(WARNING) << LOG_DESC(
                                     "generateTransactionForRotating: generate vrf-proof failed")
                              << LOG_KV("inputData", blockHash.hex());
            return SealBlockResult::FAILED;
        }

        std::string interface = precompiled::WSM_METHOD_ROTATE_STR;

        auto random = std::random_device{};
        bcos::CodecWrapper codec(_hashImpl, bcos::protocol::g_BCOSConfig.isWasm());
        auto input = codec.encodeWithSig(interface, vrfPublicKey,
            blockNumberInput ? bytes((const byte*)std::addressof(blockNumberBigEndian),
                                   (const byte*)std::addressof(blockNumberBigEndian) +
                                       sizeof(blockNumberBigEndian)) :
                               blockHash.asBytes(),
            vrfProof);

        auto tx = _sealerConfig->blockFactory()->transactionFactory()->createTransaction(0,
            std::string(bcos::protocol::g_BCOSConfig.isWasm() ? precompiled::CONSENSUS_TABLE_NAME :
                                                                precompiled::CONSENSUS_ADDRESS),
            input, std::to_string(utcSteadyTimeUs() * random()),
            _sealingManager->latestNumber() + txpool::DEFAULT_BLOCK_LIMIT, _sealerConfig->chainId(),
            _sealerConfig->groupId(), utcTime(), keyPair);

        // append in txpool, in case other peers need it
        auto& txpool = dynamic_cast<txpool::TxPool&>(*_sealerConfig->txpool());
        auto& txpoolStorage = dynamic_cast<txpool::MemoryStorage&>(*txpool.txpoolStorage());
        auto submitResult = txpoolStorage.verifyAndSubmitTransaction(tx, nullptr, false, true);
        if (submitResult != protocol::TransactionStatus::None) [[unlikely]]
        {
            SEAL_LOG(WARNING) << LOG_DESC("generateTransactionForRotating failed for txpool submit")
                              << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                              << LOG_KV("submitResult", submitResult);
            return SealBlockResult::FAILED;
        }

        // put the generated transaction into the 0th position of the block transactions
        // Note: must set generatedTx into the first transaction for other transactions may change
        //       the _sys_config_ and _sys_consensus_
        //       here must use noteChange for this function will notify updating the txsCache
        auto txMeta = _sealerConfig->blockFactory()->createTransactionMetaData(
            tx->hash(), std::string(tx->to()));
        auto address = right160(_hashImpl->hash(keyPair->publicKey()));
        // FIXME: remove source
        txMeta->setSource(address.hex());
        _block->appendTransactionMetaData(txMeta);

        SEAL_LOG(INFO) << LOG_DESC("generateTransactionForRotating succ")
                       << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                       << LOG_KV("blkNum", blockNumber) << LOG_KV("hash", blockHash.abridged())
                       << LOG_KV("nodeId", keyPair->publicKey()->hex())
                       << LOG_KV("address", address.hex());
    }
    catch (const std::exception& e)
    {
        SEAL_LOG(INFO) << LOG_DESC("generateTransactionForRotating failed")
                       << LOG_KV("nodeIdx", _sealerConfig->consensus()->nodeIndex())
                       << LOG_KV("exception", boost::diagnostic_information(e));
        return SealBlockResult::FAILED;
    }
    return SealBlockResult::SUCCESS;
}

uint16_t VRFBasedSealer::generateTransactionForRotating(bcos::protocol::Block::Ptr& _block,
    SealerConfig::Ptr const& _sealerConfig, SealingManager::ConstPtr const& _sealingManager,
    crypto::Hash::Ptr const& _hashImpl, bool blockNumberInput)
{
    // FIB-160 legacy entry point. Builds a Features view that carries the
    // explicit blockNumberInput choice the caller passed PLUS a fresh
    // (now-locked) read of feature_rpbft_vrf_type_secp256k1, then delegates
    // to the snapshot-aware overload. The two reads are not
    // snapshot-consistent with each other; new call sites should prefer the
    // RotationSnapshot-aware path via hookWhenSealBlock for full consistency.
    ledger::Features features;
    if (_sealerConfig->consensus()->consensusConfig()->features().get(
            ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1))
    {
        features.set(ledger::Features::Flag::feature_rpbft_vrf_type_secp256k1);
    }
    if (blockNumberInput)
    {
        features.set(ledger::Features::Flag::bugfix_rpbft_vrf_blocknumber_input);
    }
    return generateTransactionForRotating(
        _block, _sealerConfig, _sealingManager, _hashImpl, features);
}
}  // namespace bcos::sealer
