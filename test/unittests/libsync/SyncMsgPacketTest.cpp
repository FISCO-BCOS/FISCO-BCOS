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
 * @brief : sync msg packet test
 * @author: catli
 * @date: 2018-10-24
 */

#include <libdevcrypto/Common.h>
#include <libp2p/Common.h>
#include <libsync/SyncMsgPacket.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libsync/FakeSyncToolsSet.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace std;
using namespace dev;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::test;

namespace dev
{
namespace test
{
class SyncMsgPacketFixture : public TestOutputHelperFixture
{
public:
    SyncMsgPacketFixture() : fakeSyncToolsSet()
    {
        fakeSessionPtr = fakeSyncToolsSet.createSession();
        fakeTransactionPtr = fakeSyncToolsSet.createTransaction(0);
    }

    std::shared_ptr<SessionFace> fakeSessionPtr;
    std::shared_ptr<Transaction> fakeTransactionPtr;

private:
    FakeSyncToolsSet fakeSyncToolsSet;
};

BOOST_FIXTURE_TEST_SUITE(SyncMsgPacketTest, SyncMsgPacketFixture)

BOOST_AUTO_TEST_CASE(PacketDecodeTest)
{
    auto fakeMessagePtr = shared_ptr<Message>(nullptr);
    SyncMsgPacket msgPacket;

    // message is nullptr
    bool isSuccessful = msgPacket.decode(fakeSessionPtr, fakeMessagePtr);
    BOOST_CHECK(isSuccessful == false);

    // message contains no data
    fakeMessagePtr = make_shared<Message>();
    isSuccessful = msgPacket.decode(fakeSessionPtr, fakeMessagePtr);
    BOOST_CHECK(isSuccessful == false);

    // message is not char
    auto bufferPtr = make_shared<bytes>();
    bufferPtr->push_back(0x80);
    bufferPtr->push_back(0x20);
    fakeMessagePtr->setBuffer(bufferPtr);
    isSuccessful = msgPacket.decode(fakeSessionPtr, fakeMessagePtr);
    BOOST_CHECK(isSuccessful == false);

    // message is char
    bufferPtr->at(0) = 0x7f;
    fakeMessagePtr->setBuffer(bufferPtr);
    isSuccessful = msgPacket.decode(fakeSessionPtr, fakeMessagePtr);
    BOOST_CHECK(isSuccessful == true);
}

BOOST_AUTO_TEST_CASE(SyncStatusPacketTest)
{
    SyncStatusPacket statusPacket;
    statusPacket.encode(0x00, h256(0xab), h256(0xcd));
    auto msgPtr = statusPacket.toMessage(0x01);
    statusPacket.decode(fakeSessionPtr, msgPtr);
    auto rlpStatus = statusPacket.rlp();
    BOOST_CHECK(rlpStatus[0].toInt<int64_t>() == 0x00);
    BOOST_CHECK(rlpStatus[1].toHash<h256>() == h256(0xab));
    BOOST_CHECK(rlpStatus[2].toHash<h256>() == h256(0xcd));
}

BOOST_AUTO_TEST_CASE(SyncTransactionsPacketTest)
{
    SyncTransactionsPacket txPacket;
    bytes txRLPs = fakeTransactionPtr->rlp();

    txPacket.encode(0x01, txRLPs);
    auto msgPtr = txPacket.toMessage(0x02);
    txPacket.decode(fakeSessionPtr, msgPtr);
    auto rlpTx = txPacket.rlp()[0];
    Transaction tx;
    tx.decode(rlpTx);
    BOOST_CHECK(tx == *fakeTransactionPtr);
}

BOOST_AUTO_TEST_CASE(SyncBlocksPacketTest)
{
    SyncBlocksPacket blocksPacket;
    vector<bytes> blockRLPs;
    FakeBlock fakeBlock;

    blockRLPs.push_back(fakeBlock.getBlock().rlp());
    blocksPacket.encode(blockRLPs);
    auto msgPtr = blocksPacket.toMessage(0x03);
    blocksPacket.decode(fakeSessionPtr, msgPtr);
    RLP const& rlps = blocksPacket.rlp();
    Block block(rlps[0].toBytes());
    BOOST_CHECK(block.equalAll(fakeBlock.getBlock()));
}

BOOST_AUTO_TEST_CASE(SyncReqBlockPacketTest)
{
    SyncReqBlockPacket reqBlockPacket;

    reqBlockPacket.encode(int64_t(0x30), 0x40);
    auto msgPtr = reqBlockPacket.toMessage(0x03);
    reqBlockPacket.decode(fakeSessionPtr, msgPtr);
    auto rlpReqBlock = reqBlockPacket.rlp();
    BOOST_CHECK(rlpReqBlock[0].toInt<int64_t>() == 0x30);
    BOOST_CHECK(rlpReqBlock[1].toInt<unsigned>() == 0x40);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev