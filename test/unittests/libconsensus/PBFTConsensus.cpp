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
 * @brief: unit test for the base class of consensus module(libconsensus/Consensus.*)
 * @file: consensus.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "Common.h"
#include "FakeConsensus.h"
#include <libconsensus/pbft/PBFTConsensus.h>
#include <libethcore/Protocol.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::txpool;
using namespace dev::blockchain;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(pbftConsensusTest, TestOutputHelperFixture)

/// test constructor (invalid ProtocolID will cause exception)
/// test initPBFTEnv (normal case)
BOOST_AUTO_TEST_CASE(testInitPBFTEnvNormalCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    BOOST_CHECK(fake_pbft.consensus()->keyPair().pub() != h512());
    BOOST_CHECK(fake_pbft.consensus()->broadCastCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache());
    /// init pbft env
    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    /// check level db has already been openend
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    BOOST_CHECK(fake_pbft.consensus()->consensusBlockNumber() == 0);
    BOOST_CHECK(fake_pbft.consensus()->toView() == u256(0));
    BOOST_CHECK(fake_pbft.consensus()->view() == u256(0));
    /// check resetConfig
    BOOST_CHECK(fake_pbft.consensus()->accountType() != NodeAccountType::MinerAccount);
    fake_pbft.m_minerList.push_back(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->setMinerList(fake_pbft.m_minerList);
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->accountType() == NodeAccountType::MinerAccount);
    BOOST_CHECK(fake_pbft.consensus()->nodeIdx() == u256(fake_pbft.m_minerList.size() - 1));
    BOOST_CHECK(fake_pbft.consensus()->minerList().size() == fake_pbft.m_minerList.size());
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == u256(fake_pbft.m_minerList.size()));
    BOOST_CHECK(
        fake_pbft.consensus()->f() == u256((fake_pbft.consensus()->nodeNum() - u256(1)) / u256(3)));

    /// check reloadMsg: empty committedPrepareCache
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check m_timeManager
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime > 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime <= utcTime());
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastExecBlockFiniTime));
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastConsensusTime));
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_viewTimeout ==
                fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_changeCycle == 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastGarbageCollection <=
                std::chrono::system_clock::now());
}
/// test exception case of initPBFT
BOOST_AUTO_TEST_CASE(testInitPBFTExceptionCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    boost::filesystem::create_directories(boost::filesystem::path("./invalid"));
    fake_pbft.consensus()->setBaseDir("./invalid");
    BOOST_CHECK_THROW(fake_pbft.consensus()->initPBFTEnv(1000), NotEnoughAvailableSpace);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
