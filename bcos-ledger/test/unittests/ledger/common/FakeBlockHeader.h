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
 * @file FakeBlockHeader.h
 * @author: kyonRay
 * @date 2021-04-14
 */

#pragma once
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "bcos-protocol/Common.h"
#include "bcos-protocol/protobuf/PBBlockHeaderFactory.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
inline BlockHeader::Ptr fakeAndTestBlockHeader(CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    const ParentInfoList& _parentInfo, h256 const& _txsRoot, h256 const& _receiptsRoot,
    h256 const& _stateRoot, int64_t _number, u256 const& _gasUsed, int64_t _timestamp,
    int64_t _sealer, const std::vector<bytes>& _sealerList, bytes const& _extraData,
    SignatureList _signatureList)
{
    BlockHeaderFactory::Ptr blockHeaderFactory =
        std::make_shared<PBBlockHeaderFactory>(_cryptoSuite);
    BlockHeader::Ptr blockHeader = blockHeaderFactory->createBlockHeader();
    blockHeader->setVersion(_version);
    blockHeader->setParentInfo(_parentInfo);
    blockHeader->setTxsRoot(_txsRoot);
    blockHeader->setReceiptsRoot(_receiptsRoot);
    blockHeader->setStateRoot(_stateRoot);
    blockHeader->setNumber(_number);
    blockHeader->setGasUsed(_gasUsed);
    blockHeader->setTimestamp(_timestamp);
    blockHeader->setSealer(_sealer);
    blockHeader->setSealerList(gsl::span<const bytes>(_sealerList));
    blockHeader->setExtraData(_extraData);
    blockHeader->setSignatureList(_signatureList);
    WeightList weights;
    weights.push_back(0);
    blockHeader->setConsensusWeights(weights);
    return blockHeader;
}

inline ParentInfoList fakeParentInfo(Hash::Ptr _hashImpl, size_t _size)
{
    ParentInfoList parentInfos;
    for (size_t i = 0; i < _size; i++)
    {
        ParentInfo parentInfo;
        parentInfo.blockNumber = i;
        parentInfo.blockHash = _hashImpl->hash(std::to_string(i));
        parentInfos.emplace_back(parentInfo);
    }
    return parentInfos;
}

inline std::vector<bytes> fakeSealerList(
    std::vector<KeyPairInterface::Ptr>& _keyPairVec, SignatureCrypto::Ptr _signImpl, size_t size)
{
    std::vector<bytes> sealerList;
    for (size_t i = 0; i < size; i++)
    {
        auto keyPair = _signImpl->generateKeyPair();
        _keyPairVec.emplace_back(keyPair);
        sealerList.emplace_back(*(keyPair->publicKey()->encode()));
    }
    return sealerList;
}

inline SignatureList fakeSignatureList(SignatureCrypto::Ptr _signImpl,
    std::vector<KeyPairInterface::Ptr>& _keyPairVec, h256 const& _hash)
{
    auto sealerIndex = 0;
    SignatureList signatureList;
    for (auto keyPair : _keyPairVec)
    {
        auto signature = _signImpl->sign(keyPair, _hash);
        Signature sig;
        sig.index = sealerIndex++;
        sig.signature = *signature;
        signatureList.push_back(sig);
    }
    return signatureList;
}

inline BlockHeader::Ptr testPBBlockHeader(CryptoSuite::Ptr _cryptoSuite, BlockNumber _blockNumber)
{
    auto hashImpl = _cryptoSuite->hashImpl();
    auto signImpl = _cryptoSuite->signatureImpl();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    int version = 10;
    auto parentInfo = fakeParentInfo(hashImpl, 1);
    auto txsRoot = hashImpl->hash((std::string) "txsRoot");
    auto receiptsRoot = hashImpl->hash((std::string) "receiptsRoot");
    auto stateRoot = hashImpl->hash((std::string) "stateRoot");
    int64_t number = _blockNumber;
    u256 gasUsed = 3453456346534;
    int64_t timestamp = 9234234234;
    int64_t sealer = 100;
    std::vector<KeyPairInterface::Ptr> keyPairVec;
    auto sealerList = fakeSealerList(keyPairVec, signImpl, 4);
    bytes extraData = stateRoot.asBytes();
    auto signatureList = fakeSignatureList(signImpl, keyPairVec, receiptsRoot);

    auto blockHeader =
        fakeAndTestBlockHeader(cryptoSuite, version, parentInfo, txsRoot, receiptsRoot, stateRoot,
            number, gasUsed, timestamp, sealer, sealerList, extraData, signatureList);
    return blockHeader;
}
}  // namespace test
}  // namespace bcos
