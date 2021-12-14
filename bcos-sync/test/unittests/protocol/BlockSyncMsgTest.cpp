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
 * @brief unit test for the BlockSyncMsg
 * @file BlockSyncMsgTest.cpp
 * @author: yujiechen
 * @date 2021-06-08
 */
#include "bcos-sync/protocol/PB/BlockSyncMsgFactoryImpl.h"
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(BlockSyncMsgTest, TestPromptFixture)
inline void checkBasic(BlockSyncMsgInterface::Ptr _syncMsg, int32_t _packetType,
    BlockNumber _blockNumber, int32_t _version)
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto encodedData = _syncMsg->encode();
    auto decodedBasicMsg = factory->createBlockSyncMsg(ref(*encodedData));
    BOOST_CHECK(_syncMsg->version() == decodedBasicMsg->version());
    BOOST_CHECK(_syncMsg->packetType() == decodedBasicMsg->packetType());
    BOOST_CHECK(_syncMsg->number() == decodedBasicMsg->number());

    BOOST_CHECK(decodedBasicMsg->version() == _version);
    BOOST_CHECK(decodedBasicMsg->packetType() == _packetType);
    BOOST_CHECK(decodedBasicMsg->number() == _blockNumber);
}
inline BlockSyncMsgInterface::Ptr testSyncMsg(int32_t _packetType, BlockNumber _blockNumber,
    int32_t _version, HashType const& _hash, HashType const& _genesisHash, size_t _size,
    std::vector<bytes> _blockData)
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    BlockSyncMsgInterface::Ptr syncMsg = nullptr;
    switch (_packetType)
    {
    case BlockSyncPacketType::BlockStatusPacket:
    {
        auto statusMsg = factory->createBlockSyncStatusMsg();
        statusMsg->setHash(_hash);
        statusMsg->setGenesisHash(_genesisHash);
        syncMsg = statusMsg;
        break;
    }
    case BlockSyncPacketType::BlockRequestPacket:
    {
        auto requestMsg = factory->createBlockRequest();
        requestMsg->setSize(_size);
        syncMsg = requestMsg;
        break;
    }
    case BlockSyncPacketType::BlockResponsePacket:
    {
        auto responseMsg = factory->createBlocksMsg();
        for (auto const& data : _blockData)
        {
            responseMsg->appendBlockData(data);
        }
        syncMsg = responseMsg;
        break;
    }
    default:
    {
        return nullptr;
    }
    }
    syncMsg->setPacketType(_packetType);
    syncMsg->setNumber(_blockNumber);
    syncMsg->setVersion(_version);

    // check basic
    checkBasic(syncMsg, _packetType, _blockNumber, _version);
    auto encodedData = syncMsg->encode();
    auto decodedBasicMsg = factory->createBlockSyncMsg(ref(*encodedData));

    // check different packet
    switch (_packetType)
    {
    case BlockSyncPacketType::BlockStatusPacket:
    {
        auto statusMsg = factory->createBlockSyncStatusMsg(decodedBasicMsg);
        BOOST_CHECK(statusMsg->hash().asBytes() == _hash.asBytes());
        BOOST_CHECK(statusMsg->genesisHash().asBytes() == _genesisHash.asBytes());
        break;
    }
    case BlockSyncPacketType::BlockRequestPacket:
    {
        auto requestMsg = factory->createBlockRequest(decodedBasicMsg);
        BOOST_CHECK(requestMsg->size() == _size);
        break;
    }
    case BlockSyncPacketType::BlockResponsePacket:
    {
        auto responseMsg = factory->createBlocksMsg(decodedBasicMsg);
        BOOST_CHECK(responseMsg->blocksSize() == _blockData.size());
        size_t i = 0;
        for (auto const& data : _blockData)
        {
            auto decodedData = responseMsg->blockData(i++);
            BOOST_CHECK(data == decodedData.toBytes());
        }
        break;
    }
    default:
    {
        return nullptr;
    }
    }
    return syncMsg;
}

BOOST_AUTO_TEST_CASE(testBlockSyncMsg)
{
    BlockNumber blockNumber = 123214;
    int32_t version = 10;
    auto hashImpl = std::make_shared<Keccak256Hash>();
    HashType hash = hashImpl->hash(std::string("hash"));
    HashType genesisHash = hashImpl->hash(std::string("genesisHash"));
    size_t requestedSize = 1203;
    std::vector<bytes> blockData;
    size_t blockDataSize = 5;
    for (size_t i = 0; i < blockDataSize; i++)
    {
        std::string data = "blockData" + std::to_string(i);
        blockData.push_back(bytes(data.begin(), data.end()));
    }
    testSyncMsg(BlockSyncPacketType::BlockStatusPacket, blockNumber, version, hash, genesisHash,
        requestedSize, blockData);

    testSyncMsg(BlockSyncPacketType::BlockRequestPacket, blockNumber, version, hash, genesisHash,
        requestedSize, blockData);

    testSyncMsg(BlockSyncPacketType::BlockResponsePacket, blockNumber, version, hash, genesisHash,
        requestedSize, blockData);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos