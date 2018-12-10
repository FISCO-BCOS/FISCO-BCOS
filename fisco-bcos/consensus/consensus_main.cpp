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
#include <libdevcore/easylog.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>

using namespace dev;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;

#define CONSENSUS_MAIN_LOG(LEVEL) LOG(LEVEL) << "[#CONSENSUS_MAIN] "
static void rpcCallbackTest(dev::eth::LocalisedTransactionReceipt::Ptr receiptPtr)
{
    CONSENSUS_MAIN_LOG(TRACE) << "[#rpcCallbackTest] [blockNumber/txHash/blockHash]:  "
                              << receiptPtr->blockNumber() << "/" << receiptPtr->hash() << "/"
                              << receiptPtr->blockHash() << std::endl;
}

static void createTx(
    std::shared_ptr<LedgerManager> ledgerManager, float txSpeed, KeyPair const& key_pair)
{
    ///< transaction related
    dev::bytes rlpBytes = dev::fromHex(
        "6060604052341561000c57fe5b5b6001600060000160006101000a81548173ffffffffffffffffffffffffffff"
        "ffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506402540be40060"
        "00600101819055506002600260000160006101000a81548173ffffffffffffffffffffffffffffffffffffffff"
        "021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060006002600101819055505b5b"
        "61046b806100c26000396000f30060606040526000357c01000000000000000000000000000000000000000000"
        "00000000000000900463ffffffff168063299f7f9d146100465780638fff0fc41461006c575bfe5b341561004e"
        "57fe5b61005661008c565b6040518082815260200191505060405180910390f35b341561007457fe5b61008a60"
        "0480803590602001909190505061009a565b005b600060026001015490505b90565b8060006001015410806100"
        "b857506002600101548160026001015401105b156100c257610297565b80600060010154036000600101819055"
        "5080600260010160008282540192505081905550600480548060010182816100fa919061029a565b9160005260"
        "20600020906004020160005b608060405190810160405280604060405190810160405280600881526020017f32"
        "303137303431330000000000000000000000000000000000000000000000008152508152602001600060000160"
        "009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffff"
        "ffffffffffffffff168152602001600260000160009054906101000a900473ffffffffffffffffffffffffffff"
        "ffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018581525090919091506000"
        "8201518160000190805190602001906101fa9291906102cc565b5060208201518160010160006101000a815481"
        "73ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffff"
        "ffff16021790555060408201518160020160006101000a81548173ffffffffffffffffffffffffffffffffffff"
        "ffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506060820151816003015550"
        "50505b50565b8154818355818115116102c7576004028160040283600052602060002091820191016102c69190"
        "61034c565b5b505050565b828054600181600116156101000203166002900490600052602060002090601f0160"
        "20900481019282601f1061030d57805160ff191683800117855561033b565b8280016001018555821561033b57"
        "9182015b8281111561033a57825182559160200191906001019061031f565b5b50905061034891906103d2565b"
        "5090565b6103cf91905b808211156103cb57600060008201600061036c91906103f7565b600182016000610100"
        "0a81549073ffffffffffffffffffffffffffffffffffffffff02191690556002820160006101000a81549073ff"
        "ffffffffffffffffffffffffffffffffffffff0219169055600382016000905550600401610352565b5090565b"
        "90565b6103f491905b808211156103f05760008160009055506001016103d8565b5090565b90565b5080546001"
        "8160011615610100020316600290046000825580601f1061041d575061043c565b601f01602090049060005260"
        "206000209081019061043b91906103d2565b5b505600a165627a7a72305820d42416875dacd1e6b6035e85a8e3"
        "45ff69f7f190b7625744235de788490fd8bf0029");
    dev::eth::Transaction tx(ref(rlpBytes), dev::eth::CheckTransaction::Everything);

    /// Transaction tx(value, gasPrice, gas, dst, data);
    Secret sec = KeyPair::create().secret();
    u256 maxBlockLimit = u256(500);
    /// get the consensus status
    /// m_txSpeed default is 10
    uint16_t sleep_interval = (uint16_t)(1000.0 / txSpeed);
    u256 count = u256(0);
    while (true)
    {
        for (auto group : ledgerManager->getGrouplList())
        {
            /// set default RPC callback
            if (count % u256(50) == u256(0))
            {
                tx.setRpcCallback(boost::bind(rpcCallbackTest, _1));
            }
            try
            {
                tx.setNonce(tx.nonce() + u256(1));
                tx.setBlockLimit(u256(ledgerManager->blockChain(group)->number()) + maxBlockLimit);
                dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
                tx.updateSignature(SignatureStruct(sig));
                ledgerManager->txPool(group)->submit(tx);
            }
            catch (std::exception& e)
            {
                LOG(ERROR) << "[#SYNC_MAIN]: submit transaction failed: [EINFO]:  "
                           << boost::diagnostic_information(e) << std::endl;
            }
        }
        LogInitializer::logRotateByTime();
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
