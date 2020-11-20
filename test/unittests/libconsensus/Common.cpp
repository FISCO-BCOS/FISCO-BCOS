/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief: unit test for Common.h of libconsensus module
 * @file: Common.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "Common.h"
#include "libdevcrypto/CryptoInterface.h"
#include <libutilities/Assertions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
using namespace bcos::consensus;
namespace bcos
{
namespace test
{
template <typename T>
void checkSignAndCommitReq()
{
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq prepare_req(key_pair, 1000, 1, 134, block_hash);
    KeyPair key_pair2 = KeyPair::create();
    T checked_req(prepare_req, key_pair2, prepare_req.idx);
    BOOST_CHECK(prepare_req.sig != checked_req.sig);
    BOOST_CHECK(prepare_req.sig2 != checked_req.sig2);
    /// test encode && decode
    bytes req_data;
    BOOST_REQUIRE_NO_THROW(checked_req.encode(req_data));
    T tmp_req;
    BOOST_REQUIRE_NO_THROW(tmp_req.decode(ref(req_data)));
    BOOST_CHECK(tmp_req.height == checked_req.height);
    BOOST_CHECK(tmp_req.view == checked_req.view);
    BOOST_CHECK(tmp_req.block_hash == checked_req.block_hash);
    BOOST_CHECK(tmp_req.sig == checked_req.sig);
    BOOST_CHECK(tmp_req.sig2 == checked_req.sig2);
    BOOST_CHECK(tmp_req == checked_req);
    /// test decode exception
    req_data[0] += 1;
    BOOST_CHECK_THROW(tmp_req.decode(ref(req_data)), std::exception);
}

BOOST_FIXTURE_TEST_SUITE(consensusCommonTest, TestOutputHelperFixture)
/// testExceptions
BOOST_AUTO_TEST_CASE(testExceptions)
{
    BOOST_CHECK_THROW(
        assertThrow(false, DisabledFutureTime, "DisabledFutureTime"), DisabledFutureTime);
    BOOST_CHECK_THROW(
        assertThrow(false, OverThresTransNum, "OverThresTransNum"), OverThresTransNum);
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidBlockHeight, "InvalidBlockHeight"), InvalidBlockHeight);
    BOOST_CHECK_THROW(assertThrow(false, ExistedBlock, "ExistedBlock"), ExistedBlock);
    BOOST_CHECK_THROW(assertThrow(false, ParentNoneExist, "ParentNoneExist"), ParentNoneExist);
    BOOST_CHECK_THROW(assertThrow(false, WrongParentHash, "WrongParentHash"), WrongParentHash);
    BOOST_CHECK_THROW(
        assertThrow(false, BlockSealerListWrong, "BlockSealerListWrong"), BlockSealerListWrong);
}
/// test PBFTMsg
BOOST_AUTO_TEST_CASE(testPBFTMsg)
{
    /// test default construct
    PBFTMsg pbft_msg;
    checkPBFTMsg(pbft_msg);

    /// test encode
    h256 block_hash = crypto::Hash("block_hash");
    KeyPair key_pair = KeyPair::create();
    PBFTMsg faked_pbft_msg(key_pair, 1000, 1, 134, block_hash);
    checkPBFTMsg(faked_pbft_msg, key_pair, 1000, 1, 134, faked_pbft_msg.timestamp, block_hash);
    /// test encode
    bytes faked_pbft_data;
    faked_pbft_msg.encode(faked_pbft_data);
    BOOST_CHECK(faked_pbft_data.size());
    /// test decode
    PBFTMsg decoded_pbft_msg;
    BOOST_REQUIRE_NO_THROW(decoded_pbft_msg.decode(ref(faked_pbft_data)));
    checkPBFTMsg(decoded_pbft_msg, key_pair, 1000, 1, 134, faked_pbft_msg.timestamp, block_hash);
    /// test exception case of decode
    std::string invalid_str = "faked_pbft_msg";
    bytes invalid_data(invalid_str.begin(), invalid_str.end());
    BOOST_CHECK_THROW(faked_pbft_msg.decode(ref(invalid_data)), std::exception);

    faked_pbft_data[0] -= 1;
    BOOST_CHECK_THROW(faked_pbft_msg.decode(ref(faked_pbft_data)), std::exception);
    faked_pbft_data[0] += 1;
    BOOST_REQUIRE_NO_THROW(faked_pbft_msg.decode(ref(faked_pbft_data)));
}
/// test PrepareReq
BOOST_AUTO_TEST_CASE(testPrepareReq)
{
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq prepare_req(key_pair, 1000, 1, 134, block_hash);
    checkPBFTMsg(prepare_req, key_pair, 1000, 1, 134, prepare_req.timestamp, block_hash);
    FakeBlock fake_block(5);
    *(prepare_req.block) = fake_block.m_blockData;

    /// test encode && decode
    bytes prepare_req_data;
    BOOST_REQUIRE_NO_THROW(prepare_req.encode(prepare_req_data));
    BOOST_CHECK(prepare_req_data.size());
    /// test decode
    PrepareReq decode_prepare;
    decode_prepare.decode(ref(prepare_req_data));
    BOOST_REQUIRE_NO_THROW(decode_prepare.decode(ref(prepare_req_data)));
    checkPBFTMsg(decode_prepare, key_pair, 1000, 1, 134, prepare_req.timestamp, block_hash);
    BOOST_CHECK(*decode_prepare.block == fake_block.m_blockData);
    /// test exception case
    prepare_req_data[0] += 1;
    BOOST_CHECK_THROW(decode_prepare.decode(ref(prepare_req_data)), std::exception);

    /// test construct from given prepare req
    KeyPair key_pair2 = KeyPair::create();
    PrepareReq constructed_prepare(decode_prepare, key_pair2, 2, 135);
    checkPBFTMsg(
        constructed_prepare, key_pair2, 1000, 2, 135, constructed_prepare.timestamp, block_hash);
    BOOST_CHECK(constructed_prepare.timestamp >= decode_prepare.timestamp);
    BOOST_CHECK(*decode_prepare.block == *constructed_prepare.block);

    /// test construct prepare from given block
    PrepareReq block_populated_prepare(fake_block.m_block, key_pair2, 2, 135);
    checkPBFTMsg(block_populated_prepare, key_pair2, fake_block.m_block->blockHeader().number(), 2,
        135, block_populated_prepare.timestamp, fake_block.m_block->header().hash());
    BOOST_CHECK(block_populated_prepare.timestamp >= constructed_prepare.timestamp);
    /// test encode && decode
    block_populated_prepare.encode(prepare_req_data);
    PrepareReq tmp_req;
    BOOST_REQUIRE_NO_THROW(tmp_req.decode(ref(prepare_req_data)));
    checkPBFTMsg(tmp_req, key_pair2, fake_block.m_block->blockHeader().number(), 2, 135,
        block_populated_prepare.timestamp, fake_block.m_block->header().hash());
    Block tmp_block;
    BOOST_REQUIRE_NO_THROW(tmp_block.decode(ref(*tmp_req.block)));
    BOOST_CHECK(tmp_block.equalAll(*fake_block.m_block));

    /// test updatePrepareReq
    Sealing sealing;
    sealing.block = fake_block.m_block;
    *block_populated_prepare.block = bytes();
    sealing.p_execContext = bcos::blockverifier::ExecutiveContext::Ptr();

    PrepareReq new_req(block_populated_prepare, sealing, key_pair2);
    checkPBFTMsg(new_req, key_pair2, fake_block.m_block->blockHeader().number(), 2, 135,
        new_req.timestamp, fake_block.m_block->header().hash());
    BOOST_CHECK(new_req.timestamp >= tmp_req.timestamp);
}

/// test SignReq and CommitReq
BOOST_AUTO_TEST_CASE(testSignReqAndCommitReq)
{
    checkSignAndCommitReq<SignReq>();
    checkSignAndCommitReq<CommitReq>();
}

/// test viewchange
BOOST_AUTO_TEST_CASE(testViewChange)
{
    ViewChangeReq empty_view;
    checkPBFTMsg(empty_view);
    KeyPair key_pair = KeyPair::create();
    ViewChangeReq viewChange_req(key_pair, 100, 1000, 1, crypto::Hash("test_view"));
    checkPBFTMsg(viewChange_req, key_pair, 100, 1000, 1, viewChange_req.timestamp,
        crypto::Hash("test_view"));
    bytes req_data;
    BOOST_REQUIRE_NO_THROW(viewChange_req.encode(req_data));
    ViewChangeReq tmp_req;
    BOOST_REQUIRE_NO_THROW(tmp_req.decode(ref(req_data)));
    BOOST_CHECK(tmp_req == viewChange_req);
    /// test decode exception
    req_data[0] += 1;
    BOOST_CHECK_THROW(tmp_req.decode(ref(req_data)), std::exception);
}

/// test PBFTMsgPacket
BOOST_AUTO_TEST_CASE(testPBFTMsgPacket)
{
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq prepare_req(key_pair, 1000, 1, 134, block_hash);
    bytes prepare_data;
    prepare_req.encode(prepare_data);
    PBFTMsgPacket packet;
    packet.packet_id = PrepareReqPacket;
    packet.ttl = 10;
    packet.data = prepare_data;
    /// test encode and decode
    bytes packet_data;
    BOOST_REQUIRE_NO_THROW(packet.encode(packet_data));
    PBFTMsgPacket tmp_packet;
    tmp_packet.decode(ref(packet_data));
    BOOST_CHECK(tmp_packet.data == packet.data);
    BOOST_CHECK(tmp_packet.packet_id == packet.packet_id);
    BOOST_CHECK(tmp_packet.ttl == 10);
    BOOST_CHECK(tmp_packet == packet);
    /// test exception case
    packet_data[1] += 1;
    BOOST_CHECK_THROW(tmp_packet.decode(ref(packet_data)), std::exception);
    /// set other field
    tmp_packet.setOtherField(12, key_pair.pub(), "");
    BOOST_CHECK(tmp_packet != packet);
    BOOST_CHECK(tmp_packet.timestamp >= packet.timestamp);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(SM_consensusCommonTest, SM_CryptoTestFixture)
/// testExceptions
BOOST_AUTO_TEST_CASE(testExceptions)
{
    BOOST_CHECK_THROW(
        assertThrow(false, DisabledFutureTime, "DisabledFutureTime"), DisabledFutureTime);
    BOOST_CHECK_THROW(
        assertThrow(false, OverThresTransNum, "OverThresTransNum"), OverThresTransNum);
    BOOST_CHECK_THROW(
        assertThrow(false, InvalidBlockHeight, "InvalidBlockHeight"), InvalidBlockHeight);
    BOOST_CHECK_THROW(assertThrow(false, ExistedBlock, "ExistedBlock"), ExistedBlock);
    BOOST_CHECK_THROW(assertThrow(false, ParentNoneExist, "ParentNoneExist"), ParentNoneExist);
    BOOST_CHECK_THROW(assertThrow(false, WrongParentHash, "WrongParentHash"), WrongParentHash);
    BOOST_CHECK_THROW(
        assertThrow(false, BlockSealerListWrong, "BlockSealerListWrong"), BlockSealerListWrong);
}
/// test PBFTMsg
BOOST_AUTO_TEST_CASE(testPBFTMsg)
{
    /// test default construct
    PBFTMsg pbft_msg;
    checkPBFTMsg(pbft_msg);

    /// test encode
    h256 block_hash = crypto::Hash("block_hash");
    KeyPair key_pair = KeyPair::create();
    PBFTMsg faked_pbft_msg(key_pair, 1000, 1, 134, block_hash);
    checkPBFTMsg(faked_pbft_msg, key_pair, 1000, 1, 134, faked_pbft_msg.timestamp, block_hash);
    /// test encode
    bytes faked_pbft_data;
    faked_pbft_msg.encode(faked_pbft_data);
    BOOST_CHECK(faked_pbft_data.size());
    /// test decode
    PBFTMsg decoded_pbft_msg;
    BOOST_REQUIRE_NO_THROW(decoded_pbft_msg.decode(ref(faked_pbft_data)));
    checkPBFTMsg(decoded_pbft_msg, key_pair, 1000, 1, 134, faked_pbft_msg.timestamp, block_hash);
    /// test exception case of decode
    std::string invalid_str = "faked_pbft_msg";
    bytes invalid_data(invalid_str.begin(), invalid_str.end());
    BOOST_CHECK_THROW(faked_pbft_msg.decode(ref(invalid_data)), std::exception);

    faked_pbft_data[0] -= 1;
    BOOST_CHECK_THROW(faked_pbft_msg.decode(ref(faked_pbft_data)), std::exception);
    faked_pbft_data[0] += 1;
    BOOST_REQUIRE_NO_THROW(faked_pbft_msg.decode(ref(faked_pbft_data)));
}
/// test PrepareReq
BOOST_AUTO_TEST_CASE(testPrepareReq)
{
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq prepare_req(key_pair, 1000, 1, 134, block_hash);
    checkPBFTMsg(prepare_req, key_pair, 1000, 1, 134, prepare_req.timestamp, block_hash);
    FakeBlock fake_block(5);
    *(prepare_req.block) = fake_block.m_blockData;

    /// test encode && decode
    bytes prepare_req_data;
    BOOST_REQUIRE_NO_THROW(prepare_req.encode(prepare_req_data));
    BOOST_CHECK(prepare_req_data.size());
    /// test decode
    PrepareReq decode_prepare;
    decode_prepare.decode(ref(prepare_req_data));
    BOOST_REQUIRE_NO_THROW(decode_prepare.decode(ref(prepare_req_data)));
    checkPBFTMsg(decode_prepare, key_pair, 1000, 1, 134, prepare_req.timestamp, block_hash);
    BOOST_CHECK(*decode_prepare.block == fake_block.m_blockData);
    /// test exception case
    prepare_req_data[0] += 1;
    BOOST_CHECK_THROW(decode_prepare.decode(ref(prepare_req_data)), std::exception);

    /// test construct from given prepare req
    KeyPair key_pair2 = KeyPair::create();
    PrepareReq constructed_prepare(decode_prepare, key_pair2, 2, 135);
    checkPBFTMsg(
        constructed_prepare, key_pair2, 1000, 2, 135, constructed_prepare.timestamp, block_hash);
    BOOST_CHECK(constructed_prepare.timestamp >= decode_prepare.timestamp);
    BOOST_CHECK(*decode_prepare.block == *constructed_prepare.block);

    /// test construct prepare from given block
    PrepareReq block_populated_prepare(fake_block.m_block, key_pair2, 2, 135);
    checkPBFTMsg(block_populated_prepare, key_pair2, fake_block.m_block->blockHeader().number(), 2,
        135, block_populated_prepare.timestamp, fake_block.m_block->header().hash());
    BOOST_CHECK(block_populated_prepare.timestamp >= constructed_prepare.timestamp);
    /// test encode && decode
    block_populated_prepare.encode(prepare_req_data);
    PrepareReq tmp_req;
    BOOST_REQUIRE_NO_THROW(tmp_req.decode(ref(prepare_req_data)));
    checkPBFTMsg(tmp_req, key_pair2, fake_block.m_block->blockHeader().number(), 2, 135,
        block_populated_prepare.timestamp, fake_block.m_block->header().hash());
    Block tmp_block;
    BOOST_REQUIRE_NO_THROW(tmp_block.decode(ref(*tmp_req.block)));
    BOOST_CHECK(tmp_block.equalAll(*fake_block.m_block));

    /// test updatePrepareReq
    Sealing sealing;
    sealing.block = fake_block.m_block;
    *block_populated_prepare.block = bytes();
    sealing.p_execContext = bcos::blockverifier::ExecutiveContext::Ptr();

    PrepareReq new_req(block_populated_prepare, sealing, key_pair2);
    checkPBFTMsg(new_req, key_pair2, fake_block.m_block->blockHeader().number(), 2, 135,
        new_req.timestamp, fake_block.m_block->header().hash());
    BOOST_CHECK(new_req.timestamp >= tmp_req.timestamp);
}

/// test SignReq and CommitReq
BOOST_AUTO_TEST_CASE(testSignReqAndCommitReq)
{
    checkSignAndCommitReq<SignReq>();
    checkSignAndCommitReq<CommitReq>();
}

/// test viewchange
BOOST_AUTO_TEST_CASE(testViewChange)
{
    ViewChangeReq empty_view;
    checkPBFTMsg(empty_view);
    KeyPair key_pair = KeyPair::create();
    ViewChangeReq viewChange_req(key_pair, 100, 1000, 1, crypto::Hash("test_view"));
    checkPBFTMsg(viewChange_req, key_pair, 100, 1000, 1, viewChange_req.timestamp,
        crypto::Hash("test_view"));
    bytes req_data;
    BOOST_REQUIRE_NO_THROW(viewChange_req.encode(req_data));
    ViewChangeReq tmp_req;
    BOOST_REQUIRE_NO_THROW(tmp_req.decode(ref(req_data)));
    BOOST_CHECK(tmp_req == viewChange_req);
    /// test decode exception
    req_data[0] += 1;
    BOOST_CHECK_THROW(tmp_req.decode(ref(req_data)), std::exception);
}

/// test PBFTMsgPacket
BOOST_AUTO_TEST_CASE(testPBFTMsgPacket)
{
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq prepare_req(key_pair, 1000, 1, 134, block_hash);
    bytes prepare_data;
    prepare_req.encode(prepare_data);
    PBFTMsgPacket packet;
    packet.packet_id = PrepareReqPacket;
    packet.ttl = 10;
    packet.data = prepare_data;
    /// test encode and decode
    bytes packet_data;
    BOOST_REQUIRE_NO_THROW(packet.encode(packet_data));
    PBFTMsgPacket tmp_packet;
    tmp_packet.decode(ref(packet_data));
    BOOST_CHECK(tmp_packet.data == packet.data);
    BOOST_CHECK(tmp_packet.packet_id == packet.packet_id);
    BOOST_CHECK(tmp_packet.ttl == 10);
    BOOST_CHECK(tmp_packet == packet);
    /// test exception case
    packet_data[1] += 1;
    BOOST_CHECK_THROW(tmp_packet.decode(ref(packet_data)), std::exception);
    /// set other field
    tmp_packet.setOtherField(12, key_pair.pub(), "");
    BOOST_CHECK(tmp_packet != packet);
    BOOST_CHECK(tmp_packet.timestamp >= packet.timestamp);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
