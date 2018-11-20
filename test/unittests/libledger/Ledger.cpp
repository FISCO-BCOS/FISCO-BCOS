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
#include <fisco-bcos/Fake.h>
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
    bool initLedger() override
    {
        std::cout << "##### callbaci initLedger for FakeLedgerForTest" << std::endl;
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
        return true;
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
    /// BOOST_CHECK(param->mutableConsensusParam().intervalBlockTime == 2000);
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
    BOOST_CHECK(param->mutableStorageParam().type == "sql");
    BOOST_CHECK(param->mutableStateParam().type == "mpt");
}
/// test initConfig
BOOST_AUTO_TEST_CASE(testInitConfig)
{
#if 0
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
#endif
}

/// test initLedgers of LedgerManager
BOOST_AUTO_TEST_CASE(testInitLedger)
{
#if 0
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager =
        std::make_shared<LedgerManager>(txpool_creator.m_topicService, key_pair);
    dev::GROUP_ID group_id = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/config.group10.ini";
    ledgerManager->initSingleLedger<FakeLedgerForTest>(group_id, "", configurationPath);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(ledgerManager->getParamByGroupId(group_id));
    checkParam(param);
    /// check BlockChain
    std::shared_ptr<BlockChainInterface> m_blockChain = ledgerManager->blockChain(group_id);
    std::shared_ptr<Block> block = m_blockChain->getBlockByNumber(m_blockChain->number());
    Block populateBlock;
    populateBlock.resetCurrentBlock(block->header());
    m_blockChain->commitBlock(populateBlock, nullptr);
    BOOST_CHECK(ledgerManager->blockChain(group_id)->number() == 1);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
