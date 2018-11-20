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
#include <fisco-bcos/Fake.h>
#include <libdevcore/easylog.h>
#include <libethcore/Protocol.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/LedgerInitializer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libp2p/P2PInterface.h>
#include <libsync/Common.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::p2p;
using namespace dev::txpool;
using namespace dev::sync;
using namespace dev::blockchain;

static void createTx(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
    std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
    std::shared_ptr<dev::sync::SyncInterface> _sync, GROUP_ID const& _groupSize, float _txSpeed,
    int _totalTransactions)
{
    ///< transaction related
    bytes rlpBytes = fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
    Transaction tx(ref(rlpBytes), CheckTransaction::Everything);
    Secret sec = KeyPair::create().secret();
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
                tx.setNonce(u256(noncePrefix + to_string(txSeqNonce++)));
                tx.setBlockLimit(u256(_blockChain->number()) + 50);
                dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
                tx.updateSignature(SignatureStruct(sig));
                _txPool->submit(tx);
            }
            catch (std::exception& e)
            {
                LOG(ERROR) << "[#SYNC_MAIN]: submit transaction failed: [EINFO]:  "
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

    // NodeID nodeId = NodeID(fromHex(asString(contents(getDataDir().string() + "/node.nodeid"))));
    auto nodeIdstr = asString(contents("conf/node.nodeid"));
    NodeID nodeId = NodeID(nodeIdstr.substr(0, 128));
    LOG(INFO) << "Load node id: " << nodeIdstr << "  " << nodeId << endl;

    PROTOCOL_ID txPoolId = getGroupProtoclID(groupId, ProtocolID::TxPool);
    PROTOCOL_ID syncId = getGroupProtoclID(groupId, ProtocolID::BlockSync);

    shared_ptr<FakeBlockChain> blockChain = make_shared<FakeBlockChain>();
    shared_ptr<dev::txpool::TxPool> txPool =
        make_shared<dev::txpool::TxPool>(p2pService, blockChain, txPoolId);
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
    startSync(params);
    return 0;
}
