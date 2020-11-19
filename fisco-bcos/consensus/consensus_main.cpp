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
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::initializer;
using namespace bcos::txpool;

#define CONSENSUS_MAIN_LOG(LEVEL) LOG(LEVEL) << "[CONSENSUS_MAIN] "
static void rpcCallbackTest(bcos::eth::LocalisedTransactionReceipt::Ptr receiptPtr)
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
        if (g_BCOSConfig.version() >= RC2_VERSION)
        {
            rlpBytes = fromHex(
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
            rlpBytes = bcos::fromHex(
                "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff"
                "8609"
                "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce0000000000"
                "0000"
                "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa57"
                "97bf"
                "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d"
                "0e01"
                "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec997"
                "3574"
                "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b"
                "4b1e"
                "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9ae"
                "e206"
                "8fd63b88e817");
        }
    }
    else
    {
        if (g_BCOSConfig.version() >= RC2_VERSION)
        {
            rlpBytes = fromHex(
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
        else
        {
            rlpBytes = bcos::fromHex(
                "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
                "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
                "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
                "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
                "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
                "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
        }
    }
    bcos::eth::Transaction::Ptr tx =
        std::make_shared<bcos::eth::Transaction>(ref(rlpBytes), bcos::eth::CheckTransaction::None);

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
