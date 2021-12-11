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
 * @brief Fake the PBFT related messages
 * @file FakePBFTMessage.h
 * @author: yujiechen
 * @date 2021-04-16
 */
#pragma once
#include "bcos-pbft/core/Proposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTCodec.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessage.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTMessageFactoryImpl.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTNewViewMsg.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTProposal.h"
#include "bcos-pbft/pbft/protocol/PB/PBFTViewChangeMsg.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos;

namespace bcos
{
namespace test
{
class PBFTMessageFixture
{
public:
    using Ptr = std::shared_ptr<PBFTMessageFixture>;
    PBFTMessageFixture(CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair)
      : m_cryptoSuite(_cryptoSuite), m_keyPair(_keyPair)
    {}

    ~PBFTMessageFixture() {}

    PBFTProposal::Ptr fakePBFTProposal(BlockNumber _index, HashType const& _hash,
        bytes const& _data, std::vector<int64_t> const& _nodeList,
        std::vector<bytes> const& _signatureData)
    {
        auto pbftProposal = std::make_shared<PBFTProposal>();
        pbftProposal->setIndex(_index);
        pbftProposal->setHash(_hash);
        pbftProposal->setData(_data);
        // set signatureProof
        for (size_t i = 0; i < _nodeList.size(); i++)
        {
            pbftProposal->appendSignatureProof(_nodeList[i], ref(_signatureData[i]));
        }
        // test proposal encode/decode
        auto encodedData = pbftProposal->encode();
        auto decodedProposal = std::make_shared<PBFTProposal>(ref(*encodedData));
        BOOST_CHECK(decodedProposal->index() == pbftProposal->index());
        BOOST_CHECK(decodedProposal->hash() == pbftProposal->hash());
        BOOST_CHECK(decodedProposal->data().toBytes() == pbftProposal->data().toBytes());
        BOOST_CHECK(decodedProposal->signatureProofSize() == pbftProposal->signatureProofSize());
        return decodedProposal;
    }

    void fakeBasePBFTMessage(PBFTBaseMessageInterface::Ptr pfbtMessage, int64_t _timestamp,
        int32_t _version, int64_t _view, int64_t _generatedFrom, HashType _hash)
    {
        pfbtMessage->setTimestamp(_timestamp);
        pfbtMessage->setVersion(_version);
        pfbtMessage->setView(_view);
        pfbtMessage->setGeneratedFrom(_generatedFrom);
        pfbtMessage->setHash(_hash);
    }

    void checkBaseMessageField(
        PBFTBaseMessage::Ptr _pfbtMessage, PBFTBaseMessage::Ptr _decodedPBFTMsg)
    {
        BOOST_CHECK(_pfbtMessage->timestamp() == _decodedPBFTMsg->timestamp());
        BOOST_CHECK(_pfbtMessage->version() == _decodedPBFTMsg->version());
        BOOST_CHECK(_pfbtMessage->view() == _decodedPBFTMsg->view());
        BOOST_CHECK(_pfbtMessage->generatedFrom() == _decodedPBFTMsg->generatedFrom());
        BOOST_CHECK(_pfbtMessage->hash() == _decodedPBFTMsg->hash());
    }

    void checkProposals(
        PBFTProposalList const& _pbftProposals, PBFTProposalList const& _decodedProposals)
    {
        size_t i = 0;
        for (auto proposal : _pbftProposals)
        {
            auto comparedProposal = std::dynamic_pointer_cast<PBFTProposal>(_decodedProposals[i++]);
            BOOST_CHECK(*(std::dynamic_pointer_cast<PBFTProposal>(proposal)) == *comparedProposal);
        }
    }

    PBFTMessage::Ptr fakePBFTMessage(int64_t _timestamp, int32_t _version, int64_t _view,
        int64_t _generatedFrom, HashType _hash, PBFTProposalList const& _proposals)
    {
        auto pfbtMessage = std::make_shared<PBFTMessage>();
        fakeBasePBFTMessage(pfbtMessage, _timestamp, _version, _view, _generatedFrom, _hash);
        pfbtMessage->setProposals(_proposals);
        // encode
        auto encodedData = pfbtMessage->encode(m_cryptoSuite, m_keyPair);
        // decode
        auto decodedPBFTMsg = std::make_shared<PBFTMessage>(m_cryptoSuite, ref(*encodedData));
        // check the data fields
        checkBaseMessageField(pfbtMessage, decodedPBFTMsg);
        // check proposals
        checkProposals(_proposals, pfbtMessage->proposals());
        return decodedPBFTMsg;
    }

    PBFTNewViewMsg::Ptr fakePBFTNewViewMsg(int64_t _timestamp, int32_t _version, int64_t _view,
        int64_t _generatedFrom, HashType _hash, ViewChangeMsgList const& _viewChangeList,
        PBFTMessage::Ptr _generatedPrepare)
    {
        auto pbftNewViewChangeMsg = std::make_shared<PBFTNewViewMsg>();
        fakeBasePBFTMessage(
            pbftNewViewChangeMsg, _timestamp, _version, _view, _generatedFrom, _hash);
        pbftNewViewChangeMsg->setViewChangeMsgList(_viewChangeList);
        // encode
        auto encodedData = pbftNewViewChangeMsg->encode(nullptr, nullptr);
        // decode
        auto decodedNewViewMsg = std::make_shared<PBFTNewViewMsg>(ref(*encodedData));
        // check the basic field
        checkBaseMessageField(pbftNewViewChangeMsg, decodedNewViewMsg);
        BOOST_CHECK(decodedNewViewMsg->prePrepareList().size() == 0);

        // encode: with generatedPrePrepare
        PBFTMessageList prePrepareList;
        prePrepareList.push_back(_generatedPrepare);
        pbftNewViewChangeMsg->setPrePrepareList(prePrepareList);
        encodedData = pbftNewViewChangeMsg->encode(nullptr, nullptr);
        // decode
        decodedNewViewMsg = std::make_shared<PBFTNewViewMsg>(ref(*encodedData));
        // check the basic field
        checkBaseMessageField(pbftNewViewChangeMsg, decodedNewViewMsg);
        prePrepareList = decodedNewViewMsg->prePrepareList();
        BOOST_CHECK(prePrepareList.size() == 1);
        auto decodedPrePrepareMsg = std::dynamic_pointer_cast<PBFTMessage>(prePrepareList[0]);
        BOOST_CHECK(*_generatedPrepare == *decodedPrePrepareMsg);
        return pbftNewViewChangeMsg;
    }

    PBFTViewChangeMsg::Ptr fakePBFTViewChangeMsg(int64_t _timestamp, int32_t _version,
        int64_t _view, int64_t _generatedFrom, HashType _hash,
        PBFTProposalInterface::Ptr _committedProposal, PBFTProposalList const& _preparedProposals)
    {
        auto pbftViewChangeMsg = std::make_shared<PBFTViewChangeMsg>();
        // fake the base PBFT message
        fakeBasePBFTMessage(pbftViewChangeMsg, _timestamp, _version, _view, _generatedFrom, _hash);
        // fake the commmitted proposal

        auto committedProposal2 = std::dynamic_pointer_cast<PBFTProposal>(_committedProposal);
        pbftViewChangeMsg->setCommittedProposal(_committedProposal);
        auto committedProposal =
            std::dynamic_pointer_cast<PBFTProposal>(pbftViewChangeMsg->committedProposal());
        // the preparedProposals
        PBFTMessageList preparedMsgs;
        for (size_t i = 0; i < _preparedProposals.size(); i++)
        {
            auto message = std::make_shared<PBFTMessage>();
            message->setConsensusProposal(_preparedProposals[i]);
            preparedMsgs.push_back(message);
        }
        pbftViewChangeMsg->setPreparedProposals(preparedMsgs);
        // encode
        auto encodedData = pbftViewChangeMsg->encode(nullptr, nullptr);
        // decode
        auto decodedViewChange = std::make_shared<PBFTViewChangeMsg>(ref(*encodedData));
        // check the basic field
        checkBaseMessageField(pbftViewChangeMsg, decodedViewChange);
        // check committedProposal
        auto decodedCommittedProposal =
            std::dynamic_pointer_cast<PBFTProposal>(decodedViewChange->committedProposal());
        BOOST_CHECK(*committedProposal2 == *decodedCommittedProposal);
        // check prepared proposals
        preparedMsgs = decodedViewChange->preparedProposals();
        PBFTProposalList decodedProposalList;
        for (auto msg : preparedMsgs)
        {
            decodedProposalList.push_back(msg->consensusProposal());
        }
        checkProposals(_preparedProposals, decodedProposalList);
        return decodedViewChange;
    }
    CryptoSuite::Ptr cryptoSuite() { return m_cryptoSuite; }
    KeyPairInterface::Ptr keyPair() { return m_keyPair; }

private:
    CryptoSuite::Ptr m_cryptoSuite;
    KeyPairInterface::Ptr m_keyPair;
};

inline PBFTProposalInterface::Ptr fakeSingleProposal(CryptoSuite::Ptr _cryptoSuite,
    PBFTMessageFixture::Ptr faker,
    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> const& _nodeKeyPairList,
    BlockNumber _index, HashType const& _hash, bytes const& _data)
{
    // sign for the proposal
    std::vector<int64_t> nodeList;
    std::vector<bytes> signatureList;
    for (auto const& _nodeKeypairInfo : _nodeKeyPairList)
    {
        auto signatureData = _cryptoSuite->signatureImpl()->sign(_nodeKeypairInfo.second, _hash);
        nodeList.push_back(_nodeKeypairInfo.first);
        signatureList.push_back(*signatureData);
    }
    return faker->fakePBFTProposal(_index, _hash, _data, nodeList, signatureList);
}

inline PBFTProposalList fakeProposals(CryptoSuite::Ptr _cryptoSuite, PBFTMessageFixture::Ptr faker,
    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> const& _nodeKeyPairList,
    BlockNumber _index, bytes const& _data, size_t proposalSize)
{
    PBFTProposalList proposals;
    auto index = _index;
    auto data = _data;
    for (size_t i = 0; i < proposalSize; i++)
    {
        auto hash = _cryptoSuite->hashImpl()->hash(std::to_string(index));
        proposals.push_back(
            fakeSingleProposal(_cryptoSuite, faker, _nodeKeyPairList, index, hash, data));
        index++;
        if (data.size() > 0)
        {
            data[0] += 1;
        }
    }
    return proposals;
}

inline void checkFakedBasePBFTMessage(PBFTBaseMessageInterface::Ptr fakedMessage,
    int64_t orgTimestamp, int32_t version, ViewType view, IndexType generatedFrom,
    HashType const& proposalHash)
{
    // check the content
    BOOST_CHECK(fakedMessage->timestamp() == orgTimestamp);
    BOOST_CHECK(fakedMessage->version() == version);
    BOOST_CHECK(fakedMessage->hash() == proposalHash);
    BOOST_CHECK(fakedMessage->view() == view);
    BOOST_CHECK(fakedMessage->generatedFrom() == generatedFrom);
}

inline void checkSingleProposal(PBFTProposalInterface::Ptr _proposal, HashType const& _hash,
    BlockNumber _index, bytes const& /*_data*/)
{
    BOOST_CHECK(_proposal->index() == _index);
    BOOST_CHECK(_proposal->hash() == _hash);
    // BOOST_CHECK(_proposal->data().toBytes() == _data);
}

inline void checkProposals(PBFTProposalList _proposals, CryptoSuite::Ptr _cryptoSuite,
    BlockNumber _index, bytes const& _data)
{
    // check the proposal
    auto data = _data;
    auto index = _index;
    for (auto proposal : _proposals)
    {
        auto hash = _cryptoSuite->hashImpl()->hash(std::to_string(index));
        checkSingleProposal(proposal, hash, index, data);
        if (data.size() > 0)
        {
            data[0] += 1;
        }
        index++;
    }
}

inline void checkPBFTMessage(PBFTMessage::Ptr fakedMessage, int64_t orgTimestamp, int32_t version,
    ViewType view, IndexType generatedFrom, HashType const& proposalHash, size_t proposalSize,
    CryptoSuite::Ptr cryptoSuite, BlockNumber _index, bytes const& _data)
{
    checkFakedBasePBFTMessage(
        fakedMessage, orgTimestamp, version, view, generatedFrom, proposalHash);
    BOOST_CHECK(fakedMessage->proposals().size() == proposalSize);
    // check the proposal
    checkProposals(fakedMessage->proposals(), cryptoSuite, _index, _data);
}

inline PBFTMessage::Ptr fakePBFTMessage(int64_t orgTimestamp, int32_t version, ViewType view,
    IndexType generatedFrom, HashType const& proposalHash, BlockNumber _index, bytes const& _data,
    size_t proposalSize, std::shared_ptr<PBFTMessageFixture> _faker, PacketType _packetType)
{
    auto cryptoSuite = _faker->cryptoSuite();
    size_t signatureProofSize = 4;
    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> nodeKeyPairList;
    for (size_t i = 0; i < signatureProofSize; i++)
    {
        nodeKeyPairList.push_back(
            std::make_pair(i, cryptoSuite->signatureImpl()->generateKeyPair()));
    }
    auto proposals =
        fakeProposals(cryptoSuite, _faker, nodeKeyPairList, _index, _data, proposalSize);
    auto fakedMessage = _faker->fakePBFTMessage(
        orgTimestamp, version, view, generatedFrom, proposalHash, proposals);
    fakedMessage->setIndex(_index);
    fakedMessage->setPacketType(_packetType);
    checkPBFTMessage(fakedMessage, orgTimestamp, version, view, generatedFrom, proposalHash,
        proposalSize, cryptoSuite, _index, _data);
    return fakedMessage;
}


inline void checkViewChangeMessage(PBFTViewChangeMsg::Ptr fakedViewChangeMessage,
    int64_t orgTimestamp, int32_t version, ViewType view, IndexType generatedFrom,
    HashType const& proposalHash, BlockNumber _index, bytes const& _data,
    BlockNumber _committedIndex, HashType const& _committedHash, size_t _proposalSize,
    CryptoSuite::Ptr _cryptoSuite)
{
    checkFakedBasePBFTMessage(
        fakedViewChangeMessage, orgTimestamp, version, view, generatedFrom, proposalHash);

    // check prepared proposal
    BOOST_CHECK(fakedViewChangeMessage->preparedProposals().size() == _proposalSize);
    PBFTProposalList fakedProposals;
    for (auto msg : fakedViewChangeMessage->preparedProposals())
    {
        fakedProposals.push_back(msg->consensusProposal());
    }
    checkProposals(fakedProposals, _cryptoSuite, _index, _data);
    // check committed proposal
    checkSingleProposal(
        fakedViewChangeMessage->committedProposal(), _committedHash, _committedIndex, bytes());
}

inline PBFTViewChangeMsg::Ptr fakeViewChangeMessage(int64_t orgTimestamp, int32_t version,
    ViewType view, IndexType generatedFrom, HashType const& proposalHash, BlockNumber _index,
    bytes const& _data, BlockNumber _committedIndex, HashType const& _committedHash,
    size_t _proposalSize, std::shared_ptr<PBFTMessageFixture> _faker)
{
    auto cryptoSuite = _faker->cryptoSuite();
    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> nodeKeyPairList;
    size_t signatureProofSize = 4;
    for (size_t i = 0; i < signatureProofSize; i++)
    {
        nodeKeyPairList.push_back(
            std::make_pair(i, cryptoSuite->signatureImpl()->generateKeyPair()));
    }
    auto preparedProposals =
        fakeProposals(cryptoSuite, _faker, nodeKeyPairList, _index, _data, _proposalSize);
    std::vector<std::pair<int64_t, KeyPairInterface::Ptr>> nodeKeyPairList2;
    auto committedProposal = fakeSingleProposal(
        cryptoSuite, _faker, nodeKeyPairList2, _committedIndex, _committedHash, bytes());

    auto fakedViewChangeMessage = _faker->fakePBFTViewChangeMsg(orgTimestamp, version, view,
        generatedFrom, proposalHash, committedProposal, preparedProposals);

    checkViewChangeMessage(fakedViewChangeMessage, orgTimestamp, version, view, generatedFrom,
        proposalHash, _index, _data, _committedIndex, _committedHash, _proposalSize, cryptoSuite);
    return fakedViewChangeMessage;
}

inline void testPBFTMessage(PacketType _packetType, CryptoSuite::Ptr _cryptoSuite)
{
    int64_t orgTimestamp = utcTime();
    int32_t version = 10;
    ViewType view = 103423423423;
    IndexType generatedFrom = 10;
    auto proposalHash = _cryptoSuite->hashImpl()->hash("proposal");
    size_t proposalSize = 3;
    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto faker = std::make_shared<PBFTMessageFixture>(_cryptoSuite, keyPair);
    // for the proposal
    BlockNumber index = 100;
    std::string dataStr = "werldksjflaskjffakesdfastadfakedaat";
    bytes data(dataStr.begin(), dataStr.end());

    auto fakedMessage = fakePBFTMessage(orgTimestamp, version, view, generatedFrom, proposalHash,
        index, data, proposalSize, faker, _packetType);

    // test PBFTCodec
    PBFTMessageFactory::Ptr pbftMessageFactory = std::make_shared<PBFTMessageFactoryImpl>();
    auto pbftCodec = std::make_shared<PBFTCodec>(keyPair, _cryptoSuite, pbftMessageFactory);
    // encode and sign
    auto encodedData = pbftCodec->encode(fakedMessage, 1);
    // decode
    auto message = pbftCodec->decode(ref(*encodedData));
    PBFTMessage::Ptr decodedMsg = std::dynamic_pointer_cast<PBFTMessage>(message);
    BOOST_CHECK(decodedMsg->packetType() == _packetType);
    // check the decoded message
    checkFakedBasePBFTMessage(decodedMsg, orgTimestamp, version, view, generatedFrom, proposalHash);
    // verify the signature
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == true);
    // the signatureHash has been updated
    auto fakedHash = _cryptoSuite->hashImpl()->hash("fakedHash");
    decodedMsg->setSignatureDataHash(fakedHash);
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == false);
}

inline void testPBFTViewChangeMessage(CryptoSuite::Ptr _cryptoSuite)
{
    int64_t orgTimestamp = utcTime();
    int32_t version = 11;
    ViewType view = 23423423432;
    IndexType generatedFrom = 200;
    auto proposalHash = _cryptoSuite->hashImpl()->hash("testPBFTViewChangeMessage");
    size_t proposalSize = 4;
    BlockNumber index = 10003;
    std::string dataStr = "werldksjflaskjffakesdfastadfakedaat";
    bytes data(dataStr.begin(), dataStr.end());

    BlockNumber committedIndex = 10002;
    HashType committedHash = _cryptoSuite->hash("10002");
    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto faker = std::make_shared<PBFTMessageFixture>(_cryptoSuite, keyPair);

    auto fakedViewChangeMsg = fakeViewChangeMessage(orgTimestamp, version, view, generatedFrom,
        proposalHash, index, data, committedIndex, committedHash, proposalSize, faker);
    BOOST_CHECK(fakedViewChangeMsg->packetType() == PacketType::ViewChangePacket);

    // test PBFTCodec
    PBFTMessageFactory::Ptr pbftMessageFactory = std::make_shared<PBFTMessageFactoryImpl>();
    auto pbftCodec = std::make_shared<PBFTCodec>(keyPair, _cryptoSuite, pbftMessageFactory);
    // encode and sign
    auto encodedData = pbftCodec->encode(fakedViewChangeMsg, 1);
    // decode
    auto message = pbftCodec->decode(ref(*encodedData));
    auto decodedMsg = std::dynamic_pointer_cast<PBFTViewChangeMsg>(message);
    // check
    BOOST_CHECK(decodedMsg->packetType() == PacketType::ViewChangePacket);
    // check the decoded message
    checkViewChangeMessage(fakedViewChangeMsg, orgTimestamp, version, view, generatedFrom,
        proposalHash, index, data, committedIndex, committedHash, proposalSize, _cryptoSuite);
    // verify the signature
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == true);
    // the signatureHash has been updated
    auto fakedHash = _cryptoSuite->hashImpl()->hash("fakedHash");
    decodedMsg->setSignatureDataHash(fakedHash);
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == false);
}

inline void checkNewViewMessage(PBFTNewViewMsg::Ptr fakedNewViewMessage, int64_t orgTimestamp,
    int32_t version, ViewType view, IndexType generatedFrom, HashType const& proposalHash,
    BlockNumber _index, bytes const& _data, int64_t viewChangeSize, int64_t proposalSize,
    BlockNumber committedIndex, CryptoSuite::Ptr _cryptoSuite)
{
    checkFakedBasePBFTMessage(
        fakedNewViewMessage, orgTimestamp, version, view, generatedFrom, proposalHash);

    // check viewChangeMsgs
    BOOST_CHECK((int64_t)(fakedNewViewMessage->viewChangeMsgList().size()) == viewChangeSize);
    for (int64_t i = 0; i < viewChangeSize; i++)
    {
        auto committedHash = _cryptoSuite->hash(std::to_string(committedIndex));
        auto fakedViewChange = fakedNewViewMessage->viewChangeMsgList()[i];
        checkViewChangeMessage(std::dynamic_pointer_cast<PBFTViewChangeMsg>(fakedViewChange),
            orgTimestamp, version, view, generatedFrom, proposalHash, _index, _data, committedIndex,
            committedHash, proposalSize, _cryptoSuite);
        committedIndex++;
        committedHash = _cryptoSuite->hash(std::to_string(committedIndex));
        generatedFrom++;
        view++;
    }
}

inline void testPBFTNewViewMessage(CryptoSuite::Ptr _cryptoSuite)
{
    int64_t orgTimestamp = utcTime();
    int32_t version = 11;
    ViewType view = 23423423432;
    auto orgView = view;

    IndexType generatedFrom = 200;
    auto orgGeneratedFrom = generatedFrom;

    auto proposalHash = _cryptoSuite->hashImpl()->hash("testPBFTViewChangeMessage");
    size_t proposalSize = 4;
    BlockNumber index = 10003;
    std::string dataStr = "werldksjflaskjffakesdfastadfakedaat";
    bytes data(dataStr.begin(), dataStr.end());

    BlockNumber committedIndex = 10002;
    auto orgCommittedIndex = committedIndex;

    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto faker = std::make_shared<PBFTMessageFixture>(_cryptoSuite, keyPair);

    int64_t viewChangeSize = 4;
    ViewChangeMsgList viewChangeList;
    for (int64_t i = 0; i < viewChangeSize; i++)
    {
        auto committedHash = _cryptoSuite->hash(std::to_string(committedIndex));
        viewChangeList.push_back(fakeViewChangeMessage(orgTimestamp, version, view, generatedFrom,
            proposalHash, index, data, committedIndex, committedHash, proposalSize, faker));
        committedIndex++;
        committedHash = _cryptoSuite->hash(std::to_string(committedIndex));
        generatedFrom++;
        view++;
    }
    // fake NewViewMsg
    auto fakedPreparedMsg = fakePBFTMessage(orgTimestamp, version, view, generatedFrom,
        proposalHash, index, data, proposalSize, faker, PacketType::PrePreparePacket);
    auto fakedNewViewMsg = faker->fakePBFTNewViewMsg(orgTimestamp, version, orgView,
        orgGeneratedFrom, proposalHash, viewChangeList, fakedPreparedMsg);
    BOOST_CHECK(int64_t(fakedNewViewMsg->viewChangeMsgList().size()) == viewChangeSize);

    // test PBFTCodec
    PBFTMessageFactory::Ptr pbftMessageFactory = std::make_shared<PBFTMessageFactoryImpl>();
    auto pbftCodec = std::make_shared<PBFTCodec>(keyPair, _cryptoSuite, pbftMessageFactory);
    // encode and sign
    auto encodedData = pbftCodec->encode(fakedNewViewMsg, 1);
    // decode
    auto message = pbftCodec->decode(ref(*encodedData));
    auto decodedMsg = std::dynamic_pointer_cast<PBFTNewViewMsg>(message);
    // check
    BOOST_CHECK(decodedMsg->packetType() == PacketType::NewViewPacket);
    // check the decoded message
    checkNewViewMessage(decodedMsg, orgTimestamp, version, orgView, orgGeneratedFrom, proposalHash,
        index, data, viewChangeSize, proposalSize, orgCommittedIndex, _cryptoSuite);
    // verify the signature
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == true);
    // the signatureHash has been updated
    auto fakedHash = _cryptoSuite->hashImpl()->hash("fakedHash");
    decodedMsg->setSignatureDataHash(fakedHash);
    BOOST_CHECK(decodedMsg->verifySignature(_cryptoSuite, keyPair->publicKey()) == false);
}

inline void testPBFTRequest(CryptoSuite::Ptr _cryptoSuite, PacketType _packetType)
{
    auto timeStamp = utcTime();
    int32_t version = 12;
    ViewType view = 234234234;
    IndexType generatedFrom = 200;

    auto proposalHash = _cryptoSuite->hashImpl()->hash("testPBFTRequest");
    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto faker = std::make_shared<PBFTMessageFixture>(_cryptoSuite, keyPair);
    auto pbftMessageFactory = std::make_shared<PBFTMessageFactoryImpl>();

    auto pbftRequest = pbftMessageFactory->createPBFTRequest();
    faker->fakeBasePBFTMessage(pbftRequest, timeStamp, version, view, generatedFrom, proposalHash);
    int64_t startIndex = 1435345;
    int64_t size = 10;
    pbftRequest->setSize(size);
    pbftRequest->setIndex(startIndex);
    pbftRequest->setPacketType(_packetType);
    // encode
    auto encodedData = pbftRequest->encode(_cryptoSuite, keyPair);
    // decode
    auto decodedPBFTRequest = pbftMessageFactory->createPBFTRequest(ref(*encodedData));
    BOOST_CHECK(*(std::dynamic_pointer_cast<PBFTRequest>(decodedPBFTRequest)) ==
                *(std::dynamic_pointer_cast<PBFTRequest>(pbftRequest)));
    checkFakedBasePBFTMessage(
        decodedPBFTRequest, timeStamp, version, view, generatedFrom, proposalHash);
    BOOST_CHECK(decodedPBFTRequest->index() == startIndex);
    BOOST_CHECK(decodedPBFTRequest->size() == size);

    // encode/decode with codec
    auto pbftCodec = std::make_shared<PBFTCodec>(keyPair, _cryptoSuite, pbftMessageFactory);
    // encode and sign
    encodedData = pbftCodec->encode(pbftRequest, 1);
    // decode
    auto message = pbftCodec->decode(ref(*encodedData));
    auto decodedMsg = std::dynamic_pointer_cast<PBFTRequest>(message);
    // check the decoded message
    checkFakedBasePBFTMessage(decodedMsg, timeStamp, version, view, generatedFrom, proposalHash);
    BOOST_CHECK(decodedMsg->index() == startIndex);
    BOOST_CHECK(decodedMsg->size() == size);
}
}  // namespace test
}  // namespace bcos