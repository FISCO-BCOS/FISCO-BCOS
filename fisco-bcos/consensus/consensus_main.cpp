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
 * @brief: empty framework for main of consensus
 *
 * @file: consensus_main.cpp
 * @author: chaychen
 * @date 2018-10-09
 */

#include <fisco-bcos/ParamParse.h>
#include <libdevcore/easylog.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Fake.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitiailizer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitiailizer.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>
using namespace dev;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;
static void createTx(std::shared_ptr<LedgerManager> ledgerManager, GROUP_ID const& groupSize,
    float txSpeed, KeyPair const& key_pair)
{
    ///< transaction related
    u256 value = u256(100);
    u256 gasPrice = u256(0);
    u256 gas = u256(100000000);
    Address dst = toAddress(KeyPair::create().pub());
    std::string str = "test transaction";
    bytes data(str.begin(), str.end());
    Transaction tx(value, gasPrice, gas, dst, data);
    Secret sec = KeyPair::create().secret();
    u256 maxBlockLimit = u256(500);
    /// get the consensus status
    /// m_txSpeed default is 10
    uint16_t sleep_interval = (uint16_t)(1000.0 / txSpeed);
    while (true)
    {
        for (int i = 1; i <= groupSize; i++)
        {
            tx.setNonce(tx.nonce() + u256(1));
            tx.setBlockLimit(u256(ledgerManager->blockChain(i)->number()) + maxBlockLimit);
            dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
            tx.updateSignature(SignatureStruct(sig));
            ledgerManager->txPool(i)->submit(tx);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
    }
}

static void startConsensus(Params& params)
{
    ///< initialize component
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.conf");

    /// auto p2pInitializer = initialize->p2pInitializer();
    /// auto p2pService = p2pInitializer->p2pService();
    auto secureInitiailizer = initialize->secureInitiailizer();
    KeyPair key_pair = secureInitiailizer->keyPair();
    auto ledgerManager = initialize->ledgerInitiailizer()->ledgerManager();
    /// create transaction
    createTx(ledgerManager, params.groupSize(), params.txSpeed(), key_pair);
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);
    startConsensus(params);
}
