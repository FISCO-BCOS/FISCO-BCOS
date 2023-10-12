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
 * @author: yujiechen
 * @date: 2021-03-16
 */
#pragma once
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/bcos-crypto/hash/SM3.h"
#include "bcos-crypto/bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-protocol/Common.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-utilities/bcos-utilities/Common.h"
#include <tbb/parallel_invoke.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
inline void checkBlockHeader(BlockHeader::Ptr blockHeader, BlockHeader::Ptr decodedBlockHeader)
{
    BOOST_CHECK(decodedBlockHeader->version() == blockHeader->version());
    // BOOST_CHECK(decodedBlockHeader->parentInfo() == blockHeader->parentInfo());

    auto originParent = blockHeader->parentInfo();
    auto decodedParent = decodedBlockHeader->parentInfo();
    BOOST_CHECK_EQUAL(originParent.size(), decodedParent.size());
    for (auto [originParentInfo, decodedParentInfo] :
        RANGES::views::zip(originParent, decodedParent))
    {
        BOOST_CHECK_EQUAL(originParentInfo.blockHash, decodedParentInfo.blockHash);
        BOOST_CHECK_EQUAL(originParentInfo.blockNumber, decodedParentInfo.blockNumber);
    }
    BOOST_CHECK(decodedBlockHeader->txsRoot() == blockHeader->txsRoot());
    BOOST_CHECK(decodedBlockHeader->receiptsRoot() == blockHeader->receiptsRoot());
    BOOST_CHECK(decodedBlockHeader->stateRoot() == blockHeader->stateRoot());
    BOOST_CHECK(decodedBlockHeader->number() == blockHeader->number());
    BOOST_CHECK(decodedBlockHeader->gasUsed() == blockHeader->gasUsed());
    BOOST_CHECK(decodedBlockHeader->timestamp() == blockHeader->timestamp());
    BOOST_CHECK(decodedBlockHeader->sealer() == blockHeader->sealer());

    auto decodedSealerList = decodedBlockHeader->sealerList();
    BOOST_CHECK(decodedSealerList.size() == blockHeader->sealerList().size());
    for (decltype(decodedSealerList)::size_type i = 0; i < decodedSealerList.size(); i++)
    {
        BOOST_CHECK(decodedSealerList[i] == blockHeader->sealerList()[i]);
    }
    BOOST_CHECK(decodedBlockHeader->extraData().toBytes() == blockHeader->extraData().toBytes());
    BOOST_CHECK((decodedBlockHeader->consensusWeights()) == (blockHeader->consensusWeights()));
    // check signature
    BOOST_CHECK(decodedBlockHeader->signatureList().size() == blockHeader->signatureList().size());
    size_t index = 0;
    for (auto signature : decodedBlockHeader->signatureList())
    {
        BOOST_CHECK(signature.index == blockHeader->signatureList()[index].index);
        BOOST_CHECK(signature.signature == blockHeader->signatureList()[index].signature);
        index++;
    }
#if 0
    std::cout << "### PBBlockHeaderTest: version:" << decodedBlockHeader->version() << std::endl;
    std::cout << "### PBBlockHeaderTest: txsRoot:" << decodedBlockHeader->txsRoot().hex()
              << std::endl;
    std::cout << "### PBBlockHeaderTest: receiptsRoot:" << decodedBlockHeader->receiptsRoot().hex()
              << std::endl;
    std::cout << "### PBBlockHeaderTest: stateRoot:" << decodedBlockHeader->stateRoot().hex()
              << std::endl;
    std::cout << "### PBBlockHeaderTest: number:" << decodedBlockHeader->number() << std::endl;
    std::cout << "### PBBlockHeaderTest: gasUsed:" << decodedBlockHeader->gasUsed() << std::endl;
    std::cout << "### PBBlockHeaderTest: timestamp:" << decodedBlockHeader->timestamp()
              << std::endl;
    std::cout << "### PBBlockHeaderTest: sealer:" << decodedBlockHeader->sealer() << std::endl;
    std::cout << "### PBBlockHeaderTest: sealer:" << *toHexString(decodedBlockHeader->extraData())
              << std::endl;
    std::cout << "#### hash:" << decodedBlockHeader->hash().hex() << std::endl;
    if (blockHeader->parentInfo().size() >= 1)
    {
        std::cout << "### parentHash:" << blockHeader->parentInfo()[0].blockHash.hex() << std::endl;
    }
#endif
}

inline BlockHeader::Ptr fakeAndTestBlockHeader(CryptoSuite::Ptr _cryptoSuite, int32_t _version,
    const ParentInfoList& _parentInfo, h256 const& _txsRoot, h256 const& _receiptsRoot,
    h256 const& _stateRoot, int64_t _number, u256 const& _gasUsed, int64_t _timestamp,
    int64_t _sealer, const std::vector<bytes>& _sealerList, bytes const& _extraData,
    SignatureList _signatureList, bool _check = true)
{
    BlockHeaderFactory::Ptr blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(_cryptoSuite);
    BlockHeader::Ptr blockHeader = blockHeaderFactory->createBlockHeader();
    auto blockHeaderImpl =
        std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(blockHeader);
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
    if (_check)
    {
        BOOST_CHECK(blockHeaderImpl->inner().dataHash.empty());
    }
    blockHeader->calculateHash(*_cryptoSuite->hashImpl());

    if (_check)
    {
        tbb::parallel_invoke([blockHeader]() { blockHeader->hash(); },
            [blockHeader]() { blockHeader->hash(); }, [blockHeader]() { blockHeader->hash(); },
            [blockHeader]() { blockHeader->hash(); });
        // encode
        std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
        blockHeader->encode(*encodedData);

        bcos::bytes buffer;
        blockHeader->encode(buffer);
        BOOST_CHECK(*encodedData == buffer);

        // decode
        auto decodedBlockHeader = blockHeaderFactory->createBlockHeader(*encodedData);

        auto decodedBlockHeaderImpl =
            std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(decodedBlockHeader);

        BOOST_CHECK(!decodedBlockHeaderImpl->inner().dataHash.empty());
        decodedBlockHeader->calculateHash(*_cryptoSuite->hashImpl());
#if 0
    std::cout << "### PBBlockHeaderTest: encodedData:" << *toHexString(*encodedData) << std::endl;
#endif
        // update the hash data field
        blockHeader->setNumber(_number + 1);
        blockHeader->calculateHash(*_cryptoSuite->hashImpl());
        BOOST_CHECK(blockHeader->hash() != decodedBlockHeader->hash());
        BOOST_CHECK(blockHeader->number() == decodedBlockHeader->number() + 1);
        // recover the hash field
        blockHeader->setNumber(_number);
        blockHeader->calculateHash(*_cryptoSuite->hashImpl());
        BOOST_CHECK(blockHeader->hash() == decodedBlockHeader->hash());
        BOOST_CHECK(decodedBlockHeader->consensusWeights().size() == 1);
        BOOST_CHECK(decodedBlockHeader->consensusWeights()[0] == 0);
    }
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
        KeyPairInterface::Ptr keyPair = _signImpl->generateKeyPair();
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
        auto signature = _signImpl->sign(*keyPair, _hash);
        Signature sig;
        sig.index = sealerIndex++;
        sig.signature = *signature;
        signatureList.push_back(sig);
    }
    return signatureList;
}

inline BlockHeader::Ptr testPBBlockHeader(
    CryptoSuite::Ptr _cryptoSuite, BlockNumber _blockNumber, bool _isCheck = false)
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
            number, gasUsed, timestamp, sealer, sealerList, extraData, signatureList, _isCheck);
    return blockHeader;
}
}  // namespace test
}  // namespace bcos
