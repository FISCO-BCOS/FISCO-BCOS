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
 * @author: catli
 * @date 2019-01-15
 */
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libledger/LedgerManager.h>
#include <omp.h>
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
INITIALIZE_EASYLOGGINGPP
/*
static Transactions createTxs(std::shared_ptr<LedgerManager> ledgerManager)
{
    ///< transaction related
#ifdef FISCO_GM
    dev::bytes rlpBytes = dev::fromHex(
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817");
#else
    dev::bytes rlpBytes = dev::fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
#endif
    dev::eth::Transactions txs;
    dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::None);

    Secret sec = KeyPair::create().secret();
    u256 maxBlockLimit = u256(500);

    for (auto i = 0; i < 10000; ++i)
    {
        tx.setNonce(tx.nonce() + u256(utcTime()));
        tx.setBlockLimit(u256(ledgerManager->blockChain(1)->number()) + maxBlockLimit);
        sec = KeyPair::create().secret();
        Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        txs.push_back(tx);
    }

    return txs;
}
*/
void genTxUserAddBlock(Block& _block, size_t _userNum)
{
    Transactions txs;
    Secret sec = KeyPair::create().secret();
    for (size_t i = 0; i < _userNum; i++)
    {
        u256 value = 0;
        u256 gasPrice = 0;
        u256 gas = 10000000;
        Address dest = Address(0xffff);
        string user = to_string(i);
        u256 money = 1000000000;
        dev::eth::ContractABI abi;
        bytes data = abi.abiIn("userAdd(string,uint256)", user, money);  // add 1000000000 to user i
        u256 nonce = u256(utcTime());
        Transaction tx(value, gasPrice, gas, dest, data, nonce);
        tx.setBlockLimit(250);
        // sec = KeyPair::create().secret();
        Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        txs.push_back(tx);
    }

    _block.setTransactions(txs);
}

void initUser(size_t _userNum, BlockInfo _parentBlockInfo,
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
    std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain)
{
    Block userAddBlock;
    userAddBlock.header().setNumber(_parentBlockInfo.number + 1);
    userAddBlock.header().setParentHash(_parentBlockInfo.hash);

    genTxUserAddBlock(userAddBlock, _userNum);
    auto exeCtx = _blockVerifier->executeBlock(userAddBlock, _parentBlockInfo);
    _blockChain->commitBlock(userAddBlock, exeCtx);
}

void genTxUserTransfer(Block& _block, size_t _userNum, size_t _txNum)
{
    Transactions txs;
    Secret sec = KeyPair::create().secret();
    srand(utcTime());
    for (size_t i = 0; i < _txNum; i++)
    {
        u256 value = 0;
        u256 gasPrice = 0;
        u256 gas = 10000000;
        Address dest = Address(0xffff);
        string userFrom = to_string(rand() % _userNum);
        string userTo = to_string(rand() % _userNum);
        u256 money = 1;
        dev::eth::ContractABI abi;
        bytes data = abi.abiIn("userTransfer(string,string,uint256)", userFrom, userTo,
            money);  // add 1000000000 to user i
        u256 nonce = u256(utcTime());
        Transaction tx(value, gasPrice, gas, dest, data, nonce);
        tx.setBlockLimit(250);
        // sec = KeyPair::create().secret();
        Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        txs.push_back(tx);
    }

    _block.setTransactions(txs);
}


static void startExecute(int _totalUser, int _totalTxs)
{
    auto start = chrono::system_clock::now();
    ///< initialize component
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.ini");

    auto ledgerManager = initialize->ledgerInitializer()->ledgerManager();

    auto blockChain = ledgerManager->blockChain(1);
    auto height = blockChain->number();
    auto parentBlock = blockChain->getBlockByNumber(height);
    BlockInfo parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};

    auto blockVerifier = ledgerManager->blockVerifier(1);

    std::cout << "Creating user..." << std::endl;
    initUser(_totalUser, parentBlockInfo, blockVerifier, blockChain);

    parentBlock = blockChain->getBlockByNumber(height + 1);
    parentBlockInfo = {parentBlock->header().hash(), parentBlock->header().number(),
        parentBlock->header().stateRoot()};

    /// serial execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "serial executing txs..." << std::endl;
        blockVerifier->executeBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// serial queue transaction
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "serial queue executing txs..." << std::endl;
        blockVerifier->queueExecuteBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// parallel execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "parallel executing txs..." << std::endl;
        blockVerifier->parallelExecuteBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// parallel concurrent queue execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "parallel concurrent queue executing txs..." << std::endl;
        blockVerifier->parallelCqExecuteBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// parallel level DAG execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "parallel level DAG executing txs..." << std::endl;
        blockVerifier->parallelLevelExecuteBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// parallel openmp execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "parallel openmp executing txs..." << std::endl;
        blockVerifier->parallelOmpExecuteBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    /// serial execution
    {
        std::cout << "Generating transfer txs..." << std::endl;
        Block block;
        genTxUserTransfer(block, _totalUser, _totalTxs);
        std::cout << "serial executing txs..." << std::endl;
        blockVerifier->executeBlock(block, parentBlockInfo);
        std::cout << "Executed" << std::endl;
    }

    auto end = chrono::system_clock::now();
    auto elapsed = chrono::duration_cast<chrono::microseconds>(end - start);
    std::cout << "Elapsed: " << elapsed.count() << " us" << std::endl;
}

int main(int argc, const char* argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage:   mini-para <total user> <total txs>" << std::endl;
        std::cout << "Example: mini-para 1000 10000" << std::endl;
        return 0;
    }
    int totalUser = atoi(argv[1]);
    int totalTxs = atoi(argv[2]);
    startExecute(totalUser, totalTxs);
    return 0;
}
