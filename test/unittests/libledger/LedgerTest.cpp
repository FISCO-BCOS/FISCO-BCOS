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
 * (c) 2016-2019 fisco-dev contributors.
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
#include <libconfig/GlobalConfigure.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerManager.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::stat;
using namespace bcos::ledger;
namespace bcos
{
namespace test
{
class FakeLedgerForTest : public FakeLedger
{
public:
    FakeLedgerForTest(std::shared_ptr<bcos::p2p::P2PInterface> service,
        bcos::GROUP_ID const& _groupId, bcos::KeyPair const& _keyPair, std::string const& _baseDir)
      : FakeLedger(service, _groupId, _keyPair, _baseDir)
    {}
    /// init the ledger(called by initializer)
    bool initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams) override
    {
        m_param = _ledgerParams;
        /// init dbInitializer
        m_dbInitializer = std::make_shared<bcos::ledger::DBInitializer>(m_param, 1);
        BOOST_CHECK(m_dbInitializer->storage() == nullptr);
        BOOST_CHECK(m_dbInitializer->stateFactory() == nullptr);
        BOOST_CHECK(m_dbInitializer->executiveContextFactory() == nullptr);
        /// init blockChain
        FakeLedger::initBlockChain();
        /// intit blockVerifier
        FakeLedger::initBlockVerifier();
        /// init txPool
        FakeLedger::initTxPool();
        /// init sync
        FakeLedger::initSync();
        FakeLedger::initEventLogFilterManager();
        return true;
    }

    bool initRealLedger()
    {
        bool ret = false;
        ret = Ledger::initBlockChain();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initBlockVerifier();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initTxPool();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initSync();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::consensusInitFactory();
        if (!ret)
        {
            return false;
        }
        ret = Ledger::initEventLogFilterManager();
        return ret;
    }

    void init(std::string const& _configPath)
    {
        auto params = std::make_shared<LedgerParam>();
        params->parseGenesisConfig(_configPath);
        m_param = params;
    }

    void initIniConfig(std::string const& iniConfigFileName)
    {
        if (m_param)
        {
            auto params = std::dynamic_pointer_cast<LedgerParam>(m_param);
            params->parseIniConfig(iniConfigFileName);
        }
        else
        {
            auto params = std::make_shared<LedgerParam>();
            params->parseIniConfig(iniConfigFileName);
            m_param = params;
        }
    }
    void regenerateGenesisMark() { auto params = std::dynamic_pointer_cast<LedgerParam>(m_param); }
    void setDBInitializer(std::shared_ptr<bcos::ledger::DBInitializer> _dbInitializer)
    {
        m_dbInitializer = _dbInitializer;
    }
};

BOOST_FIXTURE_TEST_SUITE(LedgerTest, TestOutputHelperFixture)

/// test init ini config and genesis config
BOOST_AUTO_TEST_CASE(testGensisConfig)
{
    // remove the data directory to trigger rebuild the genesis block
    boost::system::error_code err;
    boost::filesystem::remove_all("./data", err);
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    bcos::GROUP_ID groupId = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.genesis";
    FakeLedgerForTest fakeLedger(txpool_creator.m_topicService, groupId, key_pair, "");
    fakeLedger.init(configurationPath);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(fakeLedger.getParam());
    /// check consensus params
    BOOST_CHECK(param->mutableConsensusParam().consensusType == "raft");
    BOOST_CHECK(param->mutableConsensusParam().maxTransactions == 2000);
    BOOST_CHECK(*toHexString(param->mutableConsensusParam().sealerList[0]) ==
                "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe63"
                "7191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61");
    BOOST_CHECK(*toHexString(param->mutableConsensusParam().sealerList[1]) ==
                "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3"
                "da6fc491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1");
    /// check state DB param
    BOOST_CHECK(param->mutableStorageParam().type == "sql");
    BOOST_CHECK(param->mutableStateParam().type == "mpt");

    /// check timestamp
    /// init genesis configuration
    fakeLedger.regenerateGenesisMark();
    std::string mark =
        "10-"
        "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191cc"
        "2aeb"
        "f4746846c0db2604adebf9c70c7f418d4d5a61,"
        "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491cc"
        "1c15"
        "f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1,-raft-";
    if (g_BCOSConfig.version() < RC3_VERSION)
    {
        mark += "sql-mpt-2000-300000000";
    }
    else
    {
        mark += "mpt-2000-300000000";
    }
    BOOST_CHECK(fakeLedger.getParam()->mutableGenesisMark() == mark);

    /// init ini config
    configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.ini";
    fakeLedger.initIniConfig(configurationPath);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxPoolParam().txPoolLimit == 150000);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxParam().enableParallel == false);
    BOOST_CHECK(fakeLedger.getParam()->mutableConsensusParam().maxTTL == 3);
    param->mutableStateParam().type = "storage";
    /// modify state to storage(the default option)
    boost::property_tree::ptree pt;
    // fakeLedger.initDBConfig(pt);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_CHECK(fakeLedger.getParam()->mutableStorageParam().type == "RocksDB");
    }
    else
    {
        fakeLedger.getParam()->mutableStorageParam().type = "LevelDB";
    }
    BOOST_CHECK(fakeLedger.getParam()->mutableStateParam().type == "storage");
    fakeLedger.initIniConfig(configurationPath);
    BOOST_CHECK(fakeLedger.getParam()->mutableTxParam().enableParallel == true);

    /// test DBInitializer
    std::shared_ptr<bcos::ledger::DBInitializer> dbInitializer =
        std::make_shared<bcos::ledger::DBInitializer>(fakeLedger.getParam(), groupId);
    /// init storageDB
    BOOST_CHECK(dbInitializer->storage() == nullptr);
    dbInitializer->initStorageDB();
    BOOST_CHECK(
        boost::filesystem::exists(fakeLedger.getParam()->mutableStorageParam().path) == true);
    BOOST_CHECK(dbInitializer->storage() != nullptr);
    /// create stateDB
    bcos::h256 genesisHash = crypto::Hash("abc");
    BOOST_CHECK(dbInitializer->stateFactory() == nullptr);
    BOOST_CHECK(dbInitializer->executiveContextFactory() == nullptr);
    /// create executiveContext and stateFactory
    dbInitializer->initState(genesisHash);
    BOOST_CHECK(dbInitializer->stateFactory() != nullptr);
    BOOST_CHECK(dbInitializer->executiveContextFactory() != nullptr);
    fakeLedger.setDBInitializer(dbInitializer);

    /// test initBlockChain
    BOOST_CHECK(fakeLedger.blockChain() == nullptr);
    /// test initBlockVerifier
    BOOST_CHECK(fakeLedger.blockVerifier() == nullptr);
    BOOST_CHECK(fakeLedger.consensus() == nullptr);
    BOOST_CHECK(fakeLedger.txPool() == nullptr);
    BOOST_CHECK(fakeLedger.sync() == nullptr);
    fakeLedger.initRealLedger();
    BOOST_CHECK(fakeLedger.blockVerifier() != nullptr);
    BOOST_CHECK(fakeLedger.blockChain() != nullptr);
    BOOST_CHECK(fakeLedger.consensus() != nullptr);
    BOOST_CHECK(fakeLedger.txPool() != nullptr);
    BOOST_CHECK(fakeLedger.sync() != nullptr);
}

/// test initLedgers of LedgerManager
BOOST_AUTO_TEST_CASE(testInitLedger)
{
    boost::system::error_code err;
    boost::filesystem::remove_all("./data", err);
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager = std::make_shared<LedgerManager>();
    bcos::GROUP_ID groupId = 10;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.10.genesis";

    std::shared_ptr<LedgerInterface> ledger =
        std::make_shared<FakeLedgerForTest>(txpool_creator.m_topicService, groupId, key_pair, "");
    auto ledgerParams = std::make_shared<LedgerParam>();
    ledgerParams->init(configurationPath);
    ledger->initLedger(ledgerParams);
    ledgerManager->insertLedger(groupId, ledger);
    std::shared_ptr<LedgerParam> param =
        std::dynamic_pointer_cast<LedgerParam>(ledgerManager->getParamByGroupId(groupId));
    /// check BlockChain
    std::shared_ptr<BlockChainInterface> m_blockChain = ledgerManager->blockChain(groupId);
    std::shared_ptr<Block> block = m_blockChain->getBlockByNumber(m_blockChain->number());
    std::shared_ptr<Block> populateBlock = std::make_shared<Block>();
    populateBlock->resetCurrentBlock(block->header());
    m_blockChain->commitBlock(populateBlock, nullptr);
    BOOST_CHECK(ledgerManager->blockChain(groupId)->number() == 1);
}

void initChannel(std::shared_ptr<LedgerInterface> ledger)
{
    auto channelServer = std::make_shared<ChannelRPCServer>();
    auto handler = std::make_shared<ChannelNetworkStatHandler>("SDK");
    channelServer->setNetworkStatHandler(handler);
    ledger->setChannelRPCServer(channelServer);
}

BOOST_AUTO_TEST_CASE(testInitStorageLevelDB)
{
    boost::system::error_code err;
    boost::filesystem::remove_all("./data", err);
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager = std::make_shared<LedgerManager>();
    bcos::GROUP_ID groupId = 11;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.11.genesis";

    std::shared_ptr<LedgerInterface> ledger =
        std::make_shared<Ledger>(txpool_creator.m_topicService, groupId, key_pair);
    auto ledgerParams = std::make_shared<LedgerParam>();
    ledgerParams->init(configurationPath);
    initChannel(ledger);
    BOOST_CHECK_NO_THROW(ledger->initLedger(ledgerParams));
}

BOOST_AUTO_TEST_CASE(testInitStorageRocksDB)
{
    boost::system::error_code err;
    boost::filesystem::remove_all("./data", err);
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager = std::make_shared<LedgerManager>();
    bcos::GROUP_ID groupId = 12;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.12.genesis";

    std::shared_ptr<LedgerInterface> ledger =
        std::make_shared<Ledger>(txpool_creator.m_topicService, groupId, key_pair);
    auto ledgerParams = std::make_shared<LedgerParam>();
    ledgerParams->init(configurationPath);
    initChannel(ledger);
    BOOST_CHECK_NO_THROW(ledger->initLedger(ledgerParams));
}

BOOST_AUTO_TEST_CASE(testInitMPTLevelDB)
{
#if 0
    TxPoolFixture txpool_creator;
    KeyPair key_pair = KeyPair::create();
    std::shared_ptr<LedgerManager> ledgerManager = std::make_shared<LedgerManager>();
    bcos::GROUP_ID groupId = 15;
    std::string configurationPath = getTestPath().string() + "/fisco-bcos-data/group.15.genesis";

    std::shared_ptr<LedgerInterface> ledger =
        std::make_shared<Ledger>(txpool_creator.m_topicService, groupId, key_pair, "");
    BOOST_CHECK_NO_THROW(ledger->initLedger(configurationPath));
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
