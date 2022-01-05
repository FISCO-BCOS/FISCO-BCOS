/*
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
#include "bcos-protocol/Common.h"
#include "bcos-protocol/protobuf/PBBlockHeaderFactory.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <tbb/parallel_invoke.h>
#include <boost/test/unit_test.hpp>
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
    BOOST_CHECK(decodedBlockHeader->parentInfo() == blockHeader->parentInfo());
    BOOST_CHECK(decodedBlockHeader->txsRoot() == blockHeader->txsRoot());
    BOOST_CHECK(decodedBlockHeader->receiptsRoot() == blockHeader->receiptsRoot());
    BOOST_CHECK(decodedBlockHeader->stateRoot() == blockHeader->stateRoot());
    BOOST_CHECK(decodedBlockHeader->number() == blockHeader->number());
    BOOST_CHECK(decodedBlockHeader->gasUsed() == blockHeader->gasUsed());
    BOOST_CHECK(decodedBlockHeader->timestamp() == blockHeader->timestamp());
    BOOST_CHECK(decodedBlockHeader->sealer() == blockHeader->sealer());

    auto decodedSealerList = decodedBlockHeader->sealerList();
    BOOST_CHECK(decodedSealerList.size() == blockHeader->sealerList().size());
    for (auto i = 0; i < decodedSealerList.size(); i++)
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
#if 0
    std::cout << "### PBBlockHeaderTest: encodedData:" << *toHexString(*encodedData) << std::endl;
#endif
    // check the data of decodedBlockHeader
    checkBlockHeader(blockHeader, decodedBlockHeader);
    // test encode exception
    (*encodedData)[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBBlockHeader>(_cryptoSuite, *encodedData), PBObjectDecodeException);

    // update the hash data field
    blockHeader->setNumber(_number + 1);
    BOOST_CHECK(blockHeader->hash() != decodedBlockHeader->hash());
    BOOST_CHECK(blockHeader->number() == decodedBlockHeader->number() + 1);
    // recover the hash field
    blockHeader->setNumber(_number);
    BOOST_CHECK(blockHeader->hash() == decodedBlockHeader->hash());
    BOOST_CHECK(decodedBlockHeader->consensusWeights().size() == 1);
    BOOST_CHECK(decodedBlockHeader->consensusWeights()[0] == 0);
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

inline BlockHeader::Ptr testPBBlockHeader(CryptoSuite::Ptr _cryptoSuite)
{
    auto hashImpl = _cryptoSuite->hashImpl();
    auto signImpl = _cryptoSuite->signatureImpl();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    int version = 10;
    auto parentInfo = fakeParentInfo(hashImpl, 3);
    auto txsRoot = hashImpl->hash((std::string) "txsRoot");
    auto receiptsRoot = hashImpl->hash((std::string) "receiptsRoot");
    auto stateRoot = hashImpl->hash((std::string) "stateRoot");
    int64_t number = 12312312412;
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

    // test verifySignatureList
    signatureList = fakeSignatureList(signImpl, keyPairVec, blockHeader->hash());
    blockHeader->setSignatureList(signatureList);
    blockHeader->verifySignatureList();

    auto invalidSignatureList = fakeSignatureList(signImpl, keyPairVec, receiptsRoot);
    blockHeader->setSignatureList(invalidSignatureList);
    BOOST_CHECK_THROW(blockHeader->verifySignatureList(), InvalidSignatureList);

    blockHeader->setSignatureList(signatureList);
    blockHeader->verifySignatureList();
    return blockHeader;
}
}  // namespace test
}  // namespace bcos
