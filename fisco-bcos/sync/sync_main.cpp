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
 * @file: sync_main.cpp
 * @author: jimmyshi
 * @date 2018-10-27
 */

#include "Fake.h"
#include "SyncParamParse.h"
#include "libdevcrypto/CryptoInterface.h"
#include <fisco-bcos/Fake.h>
#include <libconfig/GlobalConfigure.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libp2p/P2PInterface.h>
#include <libsync/Common.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>
#include <libutilities/FileUtility.h>

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::ledger;
using namespace bcos::initializer;
using namespace bcos::p2p;
using namespace bcos::txpool;
using namespace bcos::sync;
using namespace bcos::blockchain;

static void createTx(std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
    std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
    std::shared_ptr<bcos::sync::SyncInterface> _sync, GROUP_ID const& _groupSize, float _txSpeed,
    int _totalTransactions)
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
    Transaction::Ptr tx =
        std::make_shared<Transaction>(ref(rlpBytes), CheckTransaction::Everything);
    auto keyPair = KeyPair::create();
    string noncePrefix = to_string(time(NULL));

    uint16_t sleep_interval = (uint16_t)(1000.0 / _txSpeed);
    int txSeqNonce = 1000000000;
    while (_totalTransactions > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
        if (_sync->status().state != SyncState::Idle)
            continue;

        _totalTransactions -= _groupSize;
        for (int i = 1; i <= _groupSize; i++)
        {
            try
            {
                tx->setNonce(u256(noncePrefix + to_string(txSeqNonce++)));
                tx->setBlockLimit(u256(_blockChain->number()) + 50);
                auto sig = bcos::crypto::Sign(keyPair, tx->hash(WithoutSignature));
                tx->updateSignature(sig);
                _txPool->submit(tx);
            }
            catch (std::exception& e)
            {
                LOG(TRACE) << "[SYNC_MAIN]: submit transaction failed: [EINFO]:  "
                           << boost::diagnostic_information(e) << std::endl;
            }
        }
    }

    // loop forever
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
    }
}

static void startSync(Params& params)
{
    ///< initialize component
    auto initialize = std::make_shared<FakeInitializer>();
    initialize->init("./config.ini");

    auto p2pInitializer = initialize->p2pInitializer();
    std::shared_ptr<P2PInterface> p2pService = p2pInitializer->p2pService();

    GROUP_ID groupId = 1;
    std::map<GROUP_ID, h512s> groudID2NodeList;
    groudID2NodeList[groupId] = {
        h512("6a6abf9afddd087e006019c61ef15ccdf6d1df8b51cb77abddadfd442c89283f51203e88f9988a514606e"
             "f681221ff165c84f29532209ff0a8866548073d04b3"),
        h512("b996782ddf307feef401d2316a42eebf15c52254113a99bc02adea3fadcc965ba94d472b5863e6a078d85"
             "9fa14ad129e4a848d0d351cd0228f3077d1bd231684"),
        h512("55da209c504014f48a77a40fb152b7a0965b4e12662deda1956887200364e1ffd53a7b9b82931232304f8"
             "037ebf5147e9c29c0ee7244bde733ff5c68ee20648e"),
        h512("c21606c9ae25e82ba7f60ced0a61914d0a4c2a92eaa52dbc762e844cc73a535afbb5b65cea4134a65e44b"
             "49cddb3a2da84703123d12be3c00609c1755c6828a3")};
    p2pService->setGroupID2NodeList(groudID2NodeList);

    // NodeID nodeId = NodeID(*fromHexString(asString(contents(getDataDir().string() +
    // "/node.nodeid"))));
    auto nodeIdstr = asString(*readContents("conf/node.nodeid"));
    NodeID nodeId = NodeID(nodeIdstr.substr(0, 128));
    LOG(INFO) << "Load node id: " << nodeIdstr << "  " << nodeId.abridged() << endl;

    PROTOCOL_ID txPoolId = getGroupProtoclID(groupId, ProtocolID::TxPool);
    PROTOCOL_ID syncId = getGroupProtoclID(groupId, ProtocolID::BlockSync);

    shared_ptr<FakeBlockChain> blockChain = make_shared<FakeBlockChain>();
    shared_ptr<bcos::txpool::TxPool> txPool =
        make_shared<bcos::txpool::TxPool>(p2pService, blockChain, txPoolId);
    shared_ptr<FakeBlockVerifier> blockVerifier = make_shared<FakeBlockVerifier>();
    shared_ptr<SyncMaster> sync = make_shared<SyncMaster>(p2pService, txPool, blockChain,
        blockVerifier, syncId, nodeId, blockChain->numberHash(0), 1000 / params.syncSpeed());
    shared_ptr<FakeConcensus> concencus = make_shared<FakeConcensus>(
        txPool, blockChain, sync, blockVerifier, 1000 / params.blockSpeed());

    sync->start();
    LOG(INFO) << "sync started" << endl;
    concencus->start();
    LOG(INFO) << "concencus started" << endl;

    /// create transaction
    createTx(
        txPool, blockChain, sync, params.groupSize(), params.txSpeed(), params.totalTransactions());
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);

    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.ini");

    startSync(params);
    return 0;
}
