/**
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
 *
 */

/**
 * @brief: unit test for ledger
 *
 * @file Ledger.cpp
 * @author: yujiechen
 * @date 2018-10-24
 */
#include <libinitializer/Fake.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerManager.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::ledger;

namespace dev
{
namespace test
{
class FakeLedgerForTest : public FakeLedger
{
public:
    FakeLedgerForTest(std::shared_ptr<dev::p2p::P2PInterface> service,
        dev::GROUP_ID const& _groupId, dev::KeyPair const& _keyPair, std::string const& _baseDir,
        std::string const& _configFile)
      : FakeLedger(service, _groupId, _keyPair, _baseDir, _configFile)
    {}
    /// init the ledger(called by initializer)
    void initLedger(
        std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile) override
    {
        /// init dbInitializer
        m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
        /// init blockChain
        FakeLedger::initBlockChain();
        /// intit blockVerifier
        FakeLedger::initBlockVerifier();
        /// init txPool
        FakeLedger::initTxPool();
        /// init sync
        FakeLedger::initSync();
    }
    std::string const& configFileName() { return m_configFileName; }
};

BOOST_FIXTURE_TEST_SUITE(LedgerTest, TestOutputHelperFixture)
void checkParam(std::shared_ptr<LedgerParam> param)
{
    /// check basic directories
    BOOST_CHECK(param->baseDir() == "./group10/data");

    /// check consensus params
    BOOST_CHECK(param->mutableConsensusParam().consensusType == "raft");
    BOOST_CHECK(param->mutableConsensusParam().intervalBlockTime == 2000);
    BOOST_CHECK(param->mutableConsensusParam().maxTransactions == 2000);
    BOOST_CHECK(toHex(param->mutableConsensusParam().minerList[0]) ==
                "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe63"
                "7191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61");
    BOOST_CHECK(toHex(param->mutableConsensusParam().minerList[1]) ==
                "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3"
                "da6fc491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1");
    /// check tx params
    BOOST_CHECK(param->mutableTxPoolParam().txPoolLimit == 1020000);
    /// check sync params
    BOOST_CHECK(param->mutableSyncParam().idleWaitMs == 100);
    /// check state DB param
    BOOST_CHECK(param->dbType() == "AMDB");
    BOOST_CHECK(param->enableMpt() == true);
    /// check genesis param
    BOOST_CHECK(toHex(param->mutableGenesisParam().genesisHash) ==
                "633f252b048f5ac81a07f8696d9d806fae1baa2c8f665a6a07f07d7f683996ab");
    BOOST_CHECK(param->mutableGenesisParam().accountStartNonce == u256(20));
}
/// test initConfig
BOOST_AUTO_TEST_CASE(testInitConfig)
{
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    dev::GROUP_ID group_id = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
    FakeLedgerForTest fakeLedger(
        txpool_creator.m_topicService, group_id, key_pair, "", configurationPath);
    BOOST_CHECK(fakeLedger.configFileName() == configurationPath);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(fakeLedger.getParam());
    checkParam(param);
}

/// test initLedgers of LedgerManager
BOOST_AUTO_TEST_CASE(testInitLedger)
{
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>> preCompile =
        std::make_shared<std::unordered_map<dev::Address, dev::eth::PrecompiledContract>>();
    std::shared_ptr<LedgerManager<FakeLedgerForTest>> ledgerManager =
        std::make_shared<LedgerManager<FakeLedgerForTest>>(
            txpool_creator.m_topicService, key_pair, preCompile);
    dev::GROUP_ID group_id = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
    ledgerManager->initSingleLedger(group_id, "", configurationPath);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(ledgerManager->getParamByGroupId(group_id));
    checkParam(param);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
