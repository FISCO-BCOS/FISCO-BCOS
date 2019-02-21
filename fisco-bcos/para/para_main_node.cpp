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
 * @brief: a node generate parallel tx by itself
 *
 * @file: para_main_node.cpp
 * @author: jimmyshi
 * @date 2018-10-09
 */
#include "ParamParse.h"
#include <libblockchain/BlockChainImp.h>
#include <libblockverifier/BlockVerifier.h>
#include <libblockverifier/ParaTxExecutor.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/DBInitializer.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>
#include <omp.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;
using namespace dev::blockverifier;
using namespace dev::blockchain;

#define CONSENSUS_MAIN_LOG(LEVEL) LOG(LEVEL) << "[#CONSENSUS_MAIN] "

static const size_t USER_NUM = 1000;
void generateUserAddTx(std::shared_ptr<LedgerManager> ledgerManager, size_t _userNum)
{
    Secret sec = KeyPair::create().secret();
    // Generate user + receiver = _userNum
    for (size_t i = 0; i < _userNum; i++)
    {
        try
        {
            u256 value = 0;
            u256 gasPrice = 0;
            u256 gas = 10000000;
            Address dest = Address(0x1006);
            string user = to_string(i);
            u256 money = 1000000000;
            dev::eth::ContractABI abi;
            bytes data =
                abi.abiIn("userSave(string,uint256)", user, money);  // add 1000000000 to user i
            u256 nonce = u256(utcTime());
            Transaction tx(value, gasPrice, gas, dest, data, nonce);
            // sec = KeyPair::create().secret();
            Signature sig = sign(sec, tx.sha3(WithoutSignature));

            for (auto group : ledgerManager->getGrouplList())
            {
                tx.setBlockLimit(u256(ledgerManager->blockChain(group)->number()) + 250);
                tx.updateSignature(SignatureStruct(sig));
                ledgerManager->txPool(group)->submit(tx);
            }
        }
        catch (std::exception& e)
        {
            LOG(TRACE) << "[#SYNC_MAIN]: submit transaction failed: [EINFO]:  "
                       << boost::diagnostic_information(e);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
/*
static void rpcCallbackTest(dev::eth::LocalisedTransactionReceipt::Ptr receiptPtr)
{
    CONSENSUS_MAIN_LOG(TRACE) << "[#rpcCallbackTest] [blockNumber/txHash/blockHash]:  "
                              << receiptPtr->blockNumber() << "/" << receiptPtr->hash() << "/"
                              << receiptPtr->blockHash();
}
*/
static void createTx(std::shared_ptr<LedgerManager> ledgerManager, float txSpeed, KeyPair const&)
{
    /// Transaction tx(value, gasPrice, gas, dst, data);
    Secret sec = KeyPair::create().secret();
    u256 maxBlockLimit = u256(500);
    /// get the consensus status
    /// m_txSpeed default is 10
    uint16_t sleep_interval = (uint16_t)(1000.0 / txSpeed);
    // u256 count = u256(0);

    while (true)
    {
        for (auto group : ledgerManager->getGrouplList())
        {
            if (ledgerManager->blockChain(group)->number() <= 0)
                generateUserAddTx(ledgerManager, USER_NUM);


            try
            {
                u256 value = 0;
                u256 gasPrice = 0;
                u256 gas = 10000000;
                Address dest = Address(0x1006);
                string userFrom;
                string userTo;

                userFrom = to_string(rand() % USER_NUM);
                userTo = to_string(rand() % USER_NUM);

                u256 money = 1;
                dev::eth::ContractABI abi;
                bytes data = abi.abiIn("userTransfer(string,string,uint256)", userFrom, userTo,
                    money);  // add 1000000000 to user i
                u256 nonce = u256(utcTime() + rand());

                Transaction tx(value, gasPrice, gas, dest, data, nonce);
                // sec = KeyPair::create().secret();
                Signature sig = sign(sec, tx.sha3(WithoutSignature));

                tx.setBlockLimit(u256(ledgerManager->blockChain(group)->number()) + maxBlockLimit);
                tx.updateSignature(SignatureStruct(sig));
                ledgerManager->txPool(group)->submit(tx);
            }

            catch (std::exception& e)
            {
                LOG(TRACE) << "[#SYNC_MAIN]: submit transaction failed: [EINFO]:  "
                           << boost::diagnostic_information(e);
            }
        }
        LogInitializer::logRotateByTime();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
    }
}

static void startConsensus(Params& params)
{
    ///< initialize component
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.ini");

    auto secureInitializer = initialize->secureInitializer();
    KeyPair key_pair = secureInitializer->keyPair();
    auto ledgerManager = initialize->ledgerInitializer()->ledgerManager();


    /// create transaction
    createTx(ledgerManager, params.txSpeed(), key_pair);
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);
    startConsensus(params);
}
