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
 * @brief
 *
 * @file BlockVerifierTest.cpp
 * @author:
 * @date 2018-09-21
 */

#include <leveldb/db.h>
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libethcore/ABI.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libledger/DBInitializer.h>
#include <libledger/LedgerManager.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <ctime>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::storage;
using namespace dev::mptstate;
using namespace dev::executive;

namespace dev
{
namespace test
{
class FakeVerifierWithDagTransfer
{
public:
    void genTxUserAddBlock(Block& _block, size_t _userNum)
    {
        std::shared_ptr<Transactions> txs = std::make_shared<Transactions>();
        // Generate user + receiver = _userNum
        auto keyPair = KeyPair::create();
        for (size_t i = 0; i < _userNum; i++)
        {
            u256 value = 0;
            u256 gasPrice = 0;
            u256 gas = 10000000;
            Address dest = Address(0x5002);
            string usr = to_string(i);
            u256 money = 1000000000;
            dev::eth::ContractABI abi;
            bytes data =
                abi.abiIn("userSave(string,uint256)", usr, money);  // add 1000000000 to user i
            u256 nonce = u256(i);
            Transaction::Ptr tx =
                std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
            tx->setBlockLimit(250);
            auto sig = dev::crypto::Sign(keyPair, tx->sha3(WithoutSignature));
            tx->updateSignature(sig);
            tx->forceSender(Address(0x2333));
            txs->push_back(tx);
        }

        _block.setTransactions(txs);
        for (auto& tx : *_block.transactions())
            tx->sender();
    }

    void initUser(size_t _userNum, BlockInfo _parentBlockInfo,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain)
    {
        Block userAddReqBlock;
        userAddReqBlock.header().setNumber(_parentBlockInfo.number + 1);
        userAddReqBlock.header().setParentHash(_parentBlockInfo.hash);

        genTxUserAddBlock(userAddReqBlock, _userNum);
        auto exeCtx = _blockVerifier->executeBlock(userAddReqBlock, _parentBlockInfo);
        std::shared_ptr<dev::eth::Block> block = std::make_shared<dev::eth::Block>(userAddReqBlock);
        _blockChain->commitBlock(block, exeCtx);
    }

    void genTxUserTransfer(Block& _block, size_t _userNum, size_t _txNum)
    {
        std::shared_ptr<Transactions> txs = std::make_shared<Transactions>();
        auto keyPair = KeyPair::create();
        for (size_t i = 0; i < _txNum; i++)
        {
            u256 value = 0;
            u256 gasPrice = 0;
            u256 gas = 10000000;
            Address dest = Address(0x5002);
            string userFrom;
            string userTo;

            if (i > _userNum / 2)
            {
                userFrom = to_string(i % _userNum);
                userTo = to_string((i + 2) % _userNum);
            }
            else
            {
                userFrom = to_string(i % (_userNum / 2));
                userTo = to_string((_userNum / 2 + i) % (_userNum));
            }

            LOG(DEBUG) << "Transfer user-" << userFrom << " to user-" << userTo;
            u256 money = 1;
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("userTransfer(string,string,uint256)", userFrom, userTo,
                money);  // add 1000000000 to user i
            u256 nonce = u256(i);
            Transaction::Ptr tx =
                std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
            tx->setBlockLimit(250);
            auto sig = dev::crypto::Sign(keyPair, tx->sha3(WithoutSignature));
            tx->updateSignature(sig);
            tx->forceSender(Address(0x2333));
            txs->push_back(tx);
        }

        _block.setTransactions(txs);
        for (auto tx : *_block.transactions())
            tx->sender();
    }

    void canCallUserBalance(
        std::shared_ptr<BlockVerifier> _verifier, Block& _block, size_t _userNum)
    {
        // Just check no throw, no result checking

        auto keyPair = KeyPair::create();
        for (size_t i = 0; i < _userNum; i++)
        {
            u256 value = 0;
            u256 gasPrice = 0;
            u256 gas = 10000000;
            Address dest = Address(0x5002);
            string usr = to_string(i);
            dev::eth::ContractABI abi;
            bytes data = abi.abiIn("userBalance(string)", usr);  // add 1000000000 to user i
            u256 nonce = u256(i);
            Transaction::Ptr tx =
                std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
            tx->setBlockLimit(250);
            auto sig = dev::crypto::Sign(keyPair, tx->sha3(WithoutSignature));
            tx->updateSignature(sig);
            tx->forceSender(Address(0x2333));
            BOOST_CHECK_NO_THROW(_verifier->executeTransaction(_block.header(), tx));
        }
    }

    h256 executeVerifier(
        int _totalUser, int _totalTxs, const string& _storageType, bool _enablePara)
    {
        boost::property_tree::ptree pt;

        std::shared_ptr<LedgerParamInterface> params = std::make_shared<LedgerParam>();

        /// init the basic config
        /// set storage db related param
        params->mutableStorageParam().type = _storageType;
        params->mutableStorageParam().path =
            "fakeBlockVerifier/" + _storageType + "_fakestate_" + to_string(utcTime());
        /// set state db related param
        params->mutableStateParam().type = "storage";

        auto dbInitializer = std::make_shared<dev::ledger::DBInitializer>(params, 1);
        dbInitializer->initStorageDB();
        std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
        blockChain->setStateStorage(dbInitializer->storage());
        blockChain->setTableFactoryFactory(dbInitializer->tableFactoryFactory());

        GenesisBlockParam initParam = {"", dev::h512s(), dev::h512s(), "consensusType",
            "storageType", "stateType", 5000, 300000000, 0, -1, -1, 0};
        bool ret = blockChain->checkAndBuildGenesisBlock(initParam);
        BOOST_CHECK(ret);

        dev::h256 genesisHash = blockChain->getBlockByNumber(0)->headerHash();
        dbInitializer->initState(genesisHash);

        std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>(_enablePara);
        /// set params for blockverifier
        blockVerifier->setExecutiveContextFactory(dbInitializer->executiveContextFactory());
        std::shared_ptr<BlockChainImp> _blockChain =
            std::dynamic_pointer_cast<BlockChainImp>(blockChain);

        blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, _blockChain, _1));

        auto number = blockChain->number();
        auto parentBlock = blockChain->getBlockByNumber(number);
        BlockInfo parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
            parentBlock->header().stateRoot()};

        std::cout << "Creating user..." << std::endl;
        initUser(_totalUser, parentBlockInfo, blockVerifier, blockChain);

        parentBlock = blockChain->getBlockByNumber(number + 1);
        parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
            parentBlock->header().stateRoot()};

        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        block.calTransactionRoot();
        blockVerifier->executeBlock(block, parentBlockInfo);

        canCallUserBalance(blockVerifier, block, _totalUser);
        return block.header().stateRoot();
    }
};

class BlockVerifierFixture : public TestOutputHelperFixture
{
public:
    BlockVerifierFixture() : TestOutputHelperFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(BlockVerifierTest, BlockVerifierFixture)


BOOST_AUTO_TEST_CASE(executeBlockTest)
{
    FakeVerifierWithDagTransfer serialExe;
    auto serialState = serialExe.executeVerifier(4, 20, "LevelDB", false);
    cout << "Serial exec root: " << serialState << endl;


    FakeVerifierWithDagTransfer paraExe;
    auto paraState = paraExe.executeVerifier(4, 20, "LevelDB", true);
    cout << "Para exec root: " << paraExe.executeVerifier(4, 20, "LevelDB", true) << endl;

    BOOST_CHECK_EQUAL(serialState, paraState);
}

BOOST_AUTO_TEST_CASE(executeTransactionTest) {}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
