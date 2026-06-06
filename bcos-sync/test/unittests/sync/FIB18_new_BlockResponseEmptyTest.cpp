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
 * @brief FIB-18-new: BlockSyncMsgImpl::decode must reject BlockResponsePacket
 *        with empty blocksData at the decode boundary. Builds on PR #5063 which
 *        added caller-side bounds checks at blockData(0) call sites.
 *
 *        Note: BlockSyncMsgImpl::decode returns void and throws on error
 *        (PBObjectDecodeException). Tests use BOOST_CHECK_THROW /
 *        BOOST_CHECK_NO_THROW since decode has no return code.
 *
 * @file FIB18_new_BlockResponseEmptyTest.cpp
 */

#include "bcos-sync/protocol/PB/BlockSyncMsgFactoryImpl.h"
#include "bcos-sync/protocol/PB/BlockSyncMsgImpl.h"
#include "bcos-sync/protocol/PB/BlocksMsgImpl.h"
#include "bcos-sync/protocol/proto/BlockSync.pb.h"
#include "bcos-sync/utilities/Common.h"
#include <bcos-protocol/Common.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::sync;

namespace bcos
{
namespace test
{
namespace
{
// FIB-suffixed helpers for UNITY_BUILD safety.
inline bcos::bytes encodeBlockResponseFib18new(int blockCount)
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto msg = factory->createBlocksMsg();
    msg->setPacketType(BlockSyncPacketType::BlockResponsePacket);
    msg->setNumber(100);
    for (int i = 0; i < blockCount; ++i)
    {
        bcos::bytes dummy = {0xab, 0xcd, 0xef};
        msg->appendBlockData(std::move(dummy));
    }
    auto encoded = msg->encode();
    return *encoded;
}

inline bcos::bytes encodeBlockStatusFib18new()
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto status = factory->createBlockSyncStatusMsg();
    status->setPacketType(BlockSyncPacketType::BlockStatusPacket);
    status->setNumber(50);
    auto encoded = status->encode();
    return *encoded;
}

inline bcos::bytes encodeBlockRequestFib18new()
{
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto req = factory->createBlockRequest();
    req->setPacketType(BlockSyncPacketType::BlockRequestPacket);
    req->setNumber(50);
    req->setSize(10);
    auto encoded = req->encode();
    return *encoded;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB18newBlockResponseEmptyTest, TestPromptFixture)

// FIB-18-new: empty BlockResponsePacket must throw at decode boundary.
BOOST_AUTO_TEST_CASE(emptyBlockResponseThrowsAtDecode)
{
    auto encoded = encodeBlockResponseFib18new(/*blockCount=*/0);
    auto msg = std::make_shared<BlockSyncMsgImpl>();
    BOOST_CHECK_THROW(msg->decode(bcos::bytesConstRef(encoded.data(), encoded.size())),
        bcos::protocol::PBObjectDecodeException);
}

// FIB-18-new: factory wrapper variant — empty BlockResponsePacket must throw.
BOOST_AUTO_TEST_CASE(emptyBlockResponseFactoryThrowsAtDecode)
{
    auto encoded = encodeBlockResponseFib18new(/*blockCount=*/0);
    auto factory = std::make_shared<BlockSyncMsgFactoryImpl>();
    BOOST_CHECK_THROW(
        factory->createBlockSyncMsg(bcos::bytesConstRef(encoded.data(), encoded.size())),
        bcos::protocol::PBObjectDecodeException);
}

// FIB-18-new: non-empty BlockResponsePacket must decode without throwing.
BOOST_AUTO_TEST_CASE(nonEmptyBlockResponseDecodesOk)
{
    auto encoded = encodeBlockResponseFib18new(/*blockCount=*/1);
    auto msg = std::make_shared<BlocksMsgImpl>();
    BOOST_CHECK_NO_THROW(msg->decode(bcos::bytesConstRef(encoded.data(), encoded.size())));
    BOOST_CHECK_EQUAL(msg->blocksSize(), 1);
}

// FIB-18-new: BlockStatusPacket with no blocksData remains valid (only
// BlockResponsePacket requires blocksData; do not regress other packet types).
BOOST_AUTO_TEST_CASE(blockStatusWithoutBlocksDataDecodesOk)
{
    auto encoded = encodeBlockStatusFib18new();
    auto msg = std::make_shared<BlockSyncMsgImpl>();
    BOOST_CHECK_NO_THROW(msg->decode(bcos::bytesConstRef(encoded.data(), encoded.size())));
    BOOST_CHECK_EQUAL(msg->packetType(), BlockSyncPacketType::BlockStatusPacket);
}

// FIB-18-new: BlockRequestPacket with no blocksData remains valid.
BOOST_AUTO_TEST_CASE(blockRequestWithoutBlocksDataDecodesOk)
{
    auto encoded = encodeBlockRequestFib18new();
    auto msg = std::make_shared<BlockSyncMsgImpl>();
    BOOST_CHECK_NO_THROW(msg->decode(bcos::bytesConstRef(encoded.data(), encoded.size())));
    BOOST_CHECK_EQUAL(msg->packetType(), BlockSyncPacketType::BlockRequestPacket);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
