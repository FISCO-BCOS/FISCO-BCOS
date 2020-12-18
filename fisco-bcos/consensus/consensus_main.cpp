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

#include "ParamParse.h"

#include <libconfig/GlobalConfigure.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libprotocol/Protocol.h>
#include <libtxpool/TxPool.h>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::initializer;
using namespace bcos::txpool;

#define CONSENSUS_MAIN_LOG(LEVEL) LOG(LEVEL) << "[CONSENSUS_MAIN] "
static void rpcCallbackTest(bcos::protocol::LocalisedTransactionReceipt::Ptr receiptPtr)
{
    CONSENSUS_MAIN_LOG(TRACE) << "[rpcCallbackTest] [blockNumber/txHash/blockHash]:  "
                              << receiptPtr->blockNumber() << "/" << receiptPtr->hash() << "/"
                              << receiptPtr->blockHash();
}

static void createTx(std::shared_ptr<LedgerManager> ledgerManager, float txSpeed, KeyPair const&)
{
    bcos::bytes rlpBytes;
    if (g_BCOSConfig.SMCrypto())
    {
        rlpBytes = *fromHexString(
            "f90114a003eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd598411e1a300"
            "84b2"
            "d05e008201f594bab78cea98af2320ad4ee81bba8a7473e0c8c48d80a48fff0fc40000000000000000"
            "0000"
            "000000000000000000000000000000000000000000040101a48fff0fc4000000000000000000000000"
            "0000"
            "000000000000000000000000000000000004b8408234c544a9f3ce3b401a92cc7175602ce2a1e29b1e"
            "c135"
            "381c7d2a9e8f78f3edc9c06ee55252857c9a4560cb39e9d70d40f4331cace4d2b3121b967fa7a829f0"
            "a00f"
            "16d87c5065ad5c3b110ef0b97fe9a67b62443cb8ddde60d4e001a64429dc6ea03d2569e0449e9a900c"
            "2365"
            "41afb9d8a8d5e1a36844439c7076f6e75ed624256f");
    }
    else
    {
        rlpBytes = *fromHexString(
            "f8d3a003922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c8411e1a30084"
            "11e1"
            "a3008201f894d6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f80a466c99139000000000000000000"
            "0000"
            "0000000000000000000000000000000000000000040101a466c9913900000000000000000000000000"
            "0000"
            "00000000000000000000000000000000041ba08e0d3fae10412c584c977721aeda88df932b2a019f08"
            "4fed"
            "a1e0a42d199ea979a016c387f79eb85078be5db40abe1670b8b480a12c7eab719bedee212b7972f77"
            "5");
    }
    bcos::protocol::Transaction::Ptr tx = std::make_shared<bcos::protocol::Transaction>(
        ref(rlpBytes), bcos::protocol::CheckTransaction::None);

    /// Transaction tx(value, gasPrice, gas, dst, data);
    auto keyPair = KeyPair::create();
    u256 maxBlockLimit = u256(500);
    /// get the consensus status
    /// m_txSpeed default is 10
    uint16_t sleep_interval = (uint16_t)(1000.0 / txSpeed);
    u256 count = u256(0);
    while (true)
    {
        for (auto group : ledgerManager->getGroupList())
        {
            /// set default RPC callback
            if (count % u256(50) == u256(0))
            {
                tx->setRpcCallback(boost::bind(rpcCallbackTest, _1));
            }
            try
            {
                tx->setNonce(tx->nonce() + u256(utcTime()));
                tx->setBlockLimit(u256(ledgerManager->blockChain(group)->number()) + maxBlockLimit);
                auto sig = bcos::crypto::Sign(keyPair, tx->hash(WithoutSignature));
                tx->updateSignature(sig);
                ledgerManager->txPool(group)->submit(tx);
            }
            catch (std::exception& e)
            {
                LOG(TRACE) << "[SYNC_MAIN]: submit transaction failed: [EINFO]:  "
                           << boost::diagnostic_information(e);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
        count++;
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
