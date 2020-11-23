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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief: ut for NodeTimeMaintenance.cpp
 * @file : test_NodeTimeMaintenance.cpp
 * @author: yujiechen
 * @date: 2020-07-03
 */
#include <libsync/NodeTimeMaintenance.h>
#include <libsync/SyncMsgPacket.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev::sync;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(NodeTimeMaintenanceTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testTryToUpdatePeerTimeInfo)
{
    auto nodeTimeMaintenance = std::make_shared<NodeTimeMaintenance>();
    auto genesisHash = dev::keccak256("genesis");
    std::vector<SyncStatusPacket::Ptr> sycStatusVec;
    std::srand(utcTime());
    std::vector<int64_t> randomValues;
    auto currentTime = utcTime();
    LOG(INFO) << LOG_DESC("### test case1 ###");
    // case1: all the node time is error, while mine time is right
    // get ten syc-status packet from different node
    for (size_t i = 0; i < 10; i++)
    {
        auto sycnStatus = std::make_shared<SyncStatusPacket>(
            KeyPair::create().pub(), 0, genesisHash, genesisHash);
        // larger than current time (no more than 1h)
        int64_t randomValue = std::rand() % (60 * 60 * 1000);
        randomValues.push_back(randomValue);
        sycnStatus->alignedTime = currentTime + randomValue;
        sycStatusVec.push_back(sycnStatus);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycnStatus);
    }

    // get ten sync-status pacekt from different node
    for (size_t i = 0; i < 10; i++)
    {
        auto sycnStatus = std::make_shared<SyncStatusPacket>(
            KeyPair::create().pub(), 0, genesisHash, genesisHash);
        // smaller than current time (no more than 1h)
        sycnStatus->alignedTime = currentTime - (randomValues[i]);
        sycStatusVec.push_back(sycnStatus);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycnStatus);
    }
    // check the medianTimeOffset
    BOOST_CHECK(std::abs(nodeTimeMaintenance->medianTimeOffset()) < 1000);

    // case2: medianTimeOffset is the future time, after correcting the time, return to normal time
    LOG(INFO) << LOG_DESC("### test case2 ###");
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime() + 120 * 60 * 1000;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    auto status =
        std::make_shared<SyncStatusPacket>(KeyPair::create().pub(), 0, genesisHash, genesisHash);
    status->alignedTime = utcTime() + 120 * 60 * 1000;
    sycStatusVec.push_back(status);
    nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);

    int64_t delta = 1000;
    BOOST_CHECK(std::abs(nodeTimeMaintenance->medianTimeOffset()) >= (120 * 60 * 1000 - delta));
    // correcting the time
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime();
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->medianTimeOffset()) <= delta);

    // case3: medianTimeOffset is the past time, after correcting the time, return to normal time
    LOG(INFO) << LOG_DESC("### test case3 ###");
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime() - 1200 * 60 * 1000;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->medianTimeOffset()) >= (1200 * 60 * 1000 - delta));
    // correcting the time
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime();
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->medianTimeOffset()) <= delta);

    // case4: all the time-offset of the same node is within 3min, not update the cache
    LOG(INFO) << LOG_DESC("### test case4 ###");
    auto orgMedianTimeOffset = nodeTimeMaintenance->medianTimeOffset();
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime() + std::rand() % (3 * 60 * 1000 - 1000);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    BOOST_CHECK(orgMedianTimeOffset == nodeTimeMaintenance->medianTimeOffset());
    for (auto const& status : sycStatusVec)
    {
        status->alignedTime = utcTime() - std::rand() % (3 * 60 * 1000 - 1000);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(status);
    }
    BOOST_CHECK(orgMedianTimeOffset == nodeTimeMaintenance->medianTimeOffset());

    // case5: mine time is error, 5 nodes time is error, and time of the left nodes is right
    LOG(INFO) << LOG_DESC("### test case5 ###");
    int64_t rightTime = utcTime() + 1200 * 60 * 1000;
    for (size_t i = 0; i < 5; i++)
    {
        sycStatusVec[i]->alignedTime = utcTime() + std::rand() % (3 * 60 * 1000 - 1000);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    for (size_t i = 5; i < sycStatusVec.size(); i++)
    {
        sycStatusVec[i]->alignedTime = rightTime;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->getAlignedTime() - rightTime) <= delta);

    // case6: mine time is error, the time of 10 nodes are error, the time of the other nodes are
    // right
    rightTime = utcTime() + 240 * 60 * 1000;
    for (size_t i = 0; i < 9; i++)
    {
        sycStatusVec[i]->alignedTime = utcTime() + std::rand() % (3 * 60 * 1000 - 1000);
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    for (size_t i = 9; i < sycStatusVec.size(); i++)
    {
        sycStatusVec[i]->alignedTime = rightTime;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->getAlignedTime() - rightTime) <= delta);
    // correct the time
    for (size_t i = 0; i < 9; i++)
    {
        sycStatusVec[i]->alignedTime = rightTime;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->getAlignedTime() - rightTime) <= delta);

    // case7: mine time is error, the time of 11  nodes are error, and the time of the other nodes
    // are right
    rightTime = utcTime() + 360 * 60 * 1000;
    int64_t maxOffset = 0;
    for (size_t i = 0; i < 11; i++)
    {
        auto random = std::rand() % (40 * 60 * 1000);
        sycStatusVec[i]->alignedTime = utcTime() + random;
        if (random > maxOffset)
        {
            maxOffset = random;
        }
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    for (size_t i = 11; i < sycStatusVec.size(); i++)
    {
        sycStatusVec[i]->alignedTime = rightTime;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[i]);
    }
    BOOST_CHECK(nodeTimeMaintenance->medianTimeOffset() <= (maxOffset + delta));
    // a node correct the time
    sycStatusVec[0]->alignedTime = rightTime;
    nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[0]);
    BOOST_CHECK(std::abs(nodeTimeMaintenance->getAlignedTime() - rightTime) <= delta);

    // all the node correct the time
    for (size_t i = 1; i < 11; i++)
    {
        sycStatusVec[i]->alignedTime = rightTime;
        nodeTimeMaintenance->tryToUpdatePeerTimeInfo(sycStatusVec[0]);
    }
    BOOST_CHECK(std::abs(nodeTimeMaintenance->getAlignedTime() - rightTime) <= delta);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev