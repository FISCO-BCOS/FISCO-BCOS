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
 * @brief: unit test for libconsensus/pbft/PBFTMsgCache.h
 * @file: PBFTMsgCache.cpp
 * @author: yujiechen
 * @date: 2018-10-11
 */
#include "libdevcrypto/CryptoInterface.h"
#include <libconsensus/pbft/PBFTMsgCache.h>
#include <libconsensus/pbft/PBFTMsgFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev::consensus;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(consensusTest, TestOutputHelperFixture)

void checkKeyExist(PBFTBroadcastCache& cache, unsigned const& type, KeyPair const& keyPair,
    std::string const& str, bool const& insert = true, bool const& exist = true)
{
    std::string key = toHex(dev::crypto::Sign(keyPair, crypto::Hash(str))->asBytes());
    if (insert)
        cache.insertKey(keyPair.pub(), type, key);
    if (exist)
        BOOST_CHECK(cache.keyExists(keyPair.pub(), type, key));
    else
        BOOST_CHECK(!cache.keyExists(keyPair.pub(), type, key));
}

BOOST_AUTO_TEST_CASE(testInsertKey)
{
    PBFTBroadcastCache broadCast_cache;
    KeyPair key_pair = KeyPair::create();
    std::string key = toHex(dev::crypto::Sign(key_pair, crypto::Hash("test"))->asBytes());
    /// test insertKey && keyExist
    /// test PrepareReqPacket
    checkKeyExist(broadCast_cache, PrepareReqPacket, key_pair, "test1");
    checkKeyExist(broadCast_cache, PrepareReqPacket, key_pair, "test2");
    checkKeyExist(broadCast_cache, PrepareReqPacket, key_pair, "test3");
    /// test SignReqPacket
    checkKeyExist(broadCast_cache, SignReqPacket, key_pair, "test1");
    checkKeyExist(broadCast_cache, SignReqPacket, key_pair, "test2");
    checkKeyExist(broadCast_cache, SignReqPacket, key_pair, "test3");
    /// test CommitReqPacket
    checkKeyExist(broadCast_cache, CommitReqPacket, key_pair, "test1");
    checkKeyExist(broadCast_cache, CommitReqPacket, key_pair, "test2");
    checkKeyExist(broadCast_cache, CommitReqPacket, key_pair, "test3");
    /// test ViewChangeReqPacket
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test1");
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test2");
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test3");
    /// test other packet
    checkKeyExist(broadCast_cache, 100, key_pair, "test1", true, false);
    checkKeyExist(broadCast_cache, 100, key_pair, "test2", true, false);
    checkKeyExist(broadCast_cache, 1000, key_pair, "test3", true, false);
    /// clearAll
    broadCast_cache.clearAll();
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test1", false, false);
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test2", false, false);
    checkKeyExist(broadCast_cache, ViewChangeReqPacket, key_pair, "test3", false, false);
}

PBFTMsgPacket::Ptr testAndCheckPBFTMsgFactory(
    std::shared_ptr<PBFTMsgFactory> _factory, dev::h512 const& _nodeId)
{
    auto msgPacket = _factory->createPBFTMsgPacket();
    uint8_t ttl = 4;

    msgPacket->packet_id = 1;
    msgPacket->ttl = ttl;
    msgPacket->timestamp = utcTime();
    msgPacket->data = dev::fromHex("0x100");

    msgPacket->forwardNodes = std::make_shared<dev::h512s>();
    msgPacket->forwardNodes->push_back(_nodeId);
    // test encode/decode for msgPacket
    std::shared_ptr<dev::bytes> encodedData = std::make_shared<dev::bytes>();
    msgPacket->encode(*encodedData);
    auto decodedMsgPacket = _factory->createPBFTMsgPacket();
    decodedMsgPacket->decode(ref(*encodedData));

    // check decodedMsgPacket
    BOOST_CHECK(msgPacket->packet_id == decodedMsgPacket->packet_id);
    BOOST_CHECK(msgPacket->ttl == decodedMsgPacket->ttl);
    BOOST_CHECK(msgPacket->data == decodedMsgPacket->data);
    return decodedMsgPacket;
}


BOOST_AUTO_TEST_CASE(testPBFTMsgFactory)
{
    auto nodeId = dev::KeyPair::create().pub();
    // test PBFTMsgPacket
    std::shared_ptr<PBFTMsgFactory> factory = std::make_shared<PBFTMsgFactory>();
    auto msgPacket = testAndCheckPBFTMsgFactory(factory, nodeId);
    BOOST_CHECK(msgPacket->forwardNodes == nullptr);

    // test OPBFTMsgPacket
    std::shared_ptr<OPBFTMsgFactory> optimizedFactory = std::make_shared<OPBFTMsgFactory>();
    msgPacket = testAndCheckPBFTMsgFactory(optimizedFactory, nodeId);
    BOOST_CHECK(std::dynamic_pointer_cast<OPBFTMsgPacket>(msgPacket) != nullptr);
    BOOST_CHECK((*msgPacket->forwardNodes)[0] == nodeId);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
