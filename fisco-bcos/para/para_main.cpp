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
 * @brief: simple demo of para tx executor
 *
 * @file: para_main.cpp
 * @author: catli, jimmyshi
 * @date 2019-01-15
 */

#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libethcore/ABI.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libledger/DBInitializer.h>
#include <libledger/LedgerManager.h>
#include <unistd.h>
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


static shared_ptr<KeyPair> keyPair;

void genTxUserAddBlock(Block& _block, size_t _userNum)
{
    std::shared_ptr<Transactions> txs = std::make_shared<Transactions>();
    // Generate user + receiver = _userNum
    for (size_t i = 0; i < _userNum; i++)
    {
        u256 value = 0;
        u256 gasPrice = 0;
        u256 gas = 10000000;
        Address dest = Address(0x5002);
        string user = to_string(i);
        u256 money = 1000000000;
        dev::eth::ContractABI abi;
        bytes data =
            abi.abiIn("userSave(string,uint256)", user, money);  // add 1000000000 to user i
        u256 nonce = u256(utcTime());
        Transaction::Ptr tx =
            std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
        tx->setBlockLimit(250);
        auto sig = dev::crypto::Sign(*keyPair, tx->sha3(WithoutSignature));
        tx->updateSignature(sig);
        txs->push_back(tx);
    }

    _block.setTransactions(txs);
    for (auto tx : *_block.transactions())
        tx->sender();
}

void initUser(size_t _userNum, BlockInfo _parentBlockInfo,
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
    std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain)
{
    std::shared_ptr<Block> userAddBlock = std::make_shared<Block>();
    userAddBlock->header().setNumber(_parentBlockInfo.number + 1);
    userAddBlock->header().setParentHash(_parentBlockInfo.hash);

    genTxUserAddBlock(*userAddBlock, _userNum);
    auto exeCtx = _blockVerifier->executeBlock(*userAddBlock, _parentBlockInfo);
    _blockChain->commitBlock(userAddBlock, exeCtx);
}

void genTxUserTransfer(Block& _block, size_t _userNum, size_t _txNum)
{
    std::shared_ptr<Transactions> txs = std::make_shared<Transactions>();
    srand(utcTime());
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
            userFrom = to_string(rand() % _userNum);
            userTo = to_string(rand() % _userNum);
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
        u256 nonce = u256(utcTime());
        Transaction::Ptr tx =
            std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
        tx->setBlockLimit(250);
        auto sig = crypto::Sign(*keyPair, tx->sha3(WithoutSignature));
        tx->updateSignature(sig);
        txs->push_back(tx);
    }

    _block.setTransactions(txs);
    for (auto tx : *_block.transactions())
        tx->sender();
}


static void startExecute(int _totalUser, int _totalTxs)
{
    auto start = chrono::steady_clock::now();

    boost::property_tree::ptree pt;
    LogInitializer log;
    log.initLog(pt);

    std::shared_ptr<LedgerParamInterface> params = std::make_shared<LedgerParam>();

    /// init the basic config
    /// set storage db related param
    params->mutableStorageParam().type = "LevelDB";
    params->mutableStorageParam().path = "/tmp/data/block";
    /// set state db related param
    params->mutableStateParam().type = "storage";


    auto dbInitializer = std::make_shared<dev::ledger::DBInitializer>(params, 1);
    dbInitializer->initStorageDB();
    std::shared_ptr<BlockChainImp> blockChain = std::make_shared<BlockChainImp>();
    blockChain->setStateStorage(dbInitializer->storage());
    blockChain->setTableFactoryFactory(dbInitializer->tableFactoryFactory());

    params->mutableGenesisMark() = "";
    params->mutableConsensusParam().sealerList = dev::h512s();
    params->mutableConsensusParam().observerList = dev::h512s();
    params->mutableConsensusParam().consensusType = "";
    params->mutableConsensusParam().maxTransactions = 5000;
    params->mutableTxParam().txGasLimit = 300000000;
    params->mutableGenesisParam().timeStamp = 0;
    bool ret = blockChain->checkAndBuildGenesisBlock(params);
    assert(ret == true);

    dev::h256 genesisHash = blockChain->getBlockByNumber(0)->headerHash();
    dbInitializer->initState(genesisHash);

    std::shared_ptr<BlockVerifier> blockVerifier = std::make_shared<BlockVerifier>(true);
    /// set params for blockverifier
    blockVerifier->setExecutiveContextFactory(dbInitializer->executiveContextFactory());
    std::shared_ptr<BlockChainImp> _blockChain =
        std::dynamic_pointer_cast<BlockChainImp>(blockChain);
    blockVerifier->setNumberHash(boost::bind(&BlockChainImp::numberHash, _blockChain, _1));

    auto height = blockChain->number();
    auto parentBlock = blockChain->getBlockByNumber(height);
    BlockInfo parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};

    std::cout << "Creating user..." << std::endl;
    initUser(_totalUser, parentBlockInfo, blockVerifier, blockChain);

    parentBlock = blockChain->getBlockByNumber(height + 1);
    parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};

    std::shared_ptr<Block> block = std::make_shared<Block>();
    genTxUserTransfer(*block, _totalUser, _totalTxs);
    // while (true)
    // getchar();
    for (int i = 0; i < 10; i++)
    {
        blockVerifier->serialExecuteBlock(*block, parentBlockInfo);
    }
    for (int i = 0; i < 10; i++)
    {
        blockVerifier->parallelExecuteBlock(*block, parentBlockInfo);
    }
    auto end = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::microseconds>(end - start);
    std::cout << "Elapsed: " << elapsed.count() << " us" << std::endl;
    exit(0);
}

int main(int argc, const char* argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage:   mini-para <total user> <total txs>" << std::endl;
        std::cout << "Example: mini-para 1000 10000" << std::endl;
        return 0;
    }
    keyPair = make_shared<KeyPair>(KeyPair::create());
    int totalUser = atoi(argv[1]);
    int totalTxs = atoi(argv[2]);
    startExecute(totalUser, totalTxs);
    return 0;
}
