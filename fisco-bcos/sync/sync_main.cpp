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
#include <libinitializer/LedgerInitiailizer.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libledger/LedgerManager.h>
#include <libp2p/P2PInterface.h>
#include <libsync/Common.h>
#include <libsync/SyncMaster.h>
#include <libtxpool/TxPool.h>


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
    srand(time(NULL));
    string randPrefix = to_string(time(NULL));

    uint16_t sleep_interval = (uint16_t)(1000.0 / _txSpeed);
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
                tx.setNonce(u256(randPrefix + to_string(rand())));
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
    shared_ptr<P2PInterface> p2pService = p2pInitializer->p2pService();

    GROUP_ID groupId = 1;
    std::map<GROUP_ID, h512s> groudID2NodeList;
    groudID2NodeList[groupId] = {
        h512("e961da2f050bffc81fdae4bed17adfe53f8b3112cc8d720d9379d8503fe41325e2fce3f93b571d8341a4b"
             "97b3bc7f3edee123635b56b2ada6ca97a9306835f93"),
        h512("1ac670bc00b477601b28b2a63e02359b5f6e2b25ce07f98601100cf252d9af484dfec2b65ae46dfa6d3a4"
             "1f44a8952e3d1ea72a0a718dbeed93aacedd4717726"),
        h512("051f68a0950bdc86a415f7a15039b9397705e6e8e6b3f37f5bf1460fbec1cad2c16581ccb5e98530c6509"
             "b7410fa42c38df882a0247515850b3b7286f357f5f8"),
        h512("2fb8fb18be34bbc0eec1163b30520fbf4d845b7c59b95fd6dc4612c22b4682f0650a2bd754c3dd68b59a5"
             "9d328c3184948c69e950bb107cca1b8c89f89e7c164")};
    p2pService->setGroupID2NodeList(groudID2NodeList);

    // NodeID nodeId = NodeID(fromHex(asString(contents(getDataDir().string() + "/node.nodeid"))));
    auto nodeIdstr = asString(contents(getDataDir().string() + "/node.nodeid"));
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
