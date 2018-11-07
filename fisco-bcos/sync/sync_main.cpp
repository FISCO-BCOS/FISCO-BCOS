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
#include <libinitializer/SecureInitiailizer.h>
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
    std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain, GROUP_ID const& _groupSize,
    float _txSpeed, int _totalTransactions, KeyPair const& key_pair)
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
    Secret sec = key_pair.secret();
    srand(time(NULL));

    uint16_t sleep_interval = (uint16_t)(1000.0 / _txSpeed);
    while (_totalTransactions > 0)
    {
        _totalTransactions -= _groupSize;
        for (int i = 1; i <= _groupSize; i++)
        {
            tx.setNonce(u256(rand()));
            tx.setBlockLimit(u256(_blockChain->number()) + 50);
            dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
            tx.updateSignature(SignatureStruct(sig));
            _txPool->submit(tx);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
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
    initialize->init("./config.conf");

    auto p2pInitializer = initialize->p2pInitializer();
    shared_ptr<P2PInterface> p2pService = p2pInitializer->p2pService();

    GROUP_ID groupId = 1;
    std::map<GROUP_ID, h512s> groudID2NodeList;
    groudID2NodeList[groupId] = {
        h512("7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191c"
             "c2aebf4746846c0db2604adebf9c70c7f418d4d5a61"),
        h512("46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491c"
             "c1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1"),
        h512("f6f4931f56b9963851f43bb857ed5a6170ec1a4208ddcf1a1f2bb66f6d7e7a5c4749a89b5277d6265b1c1"
             "2fdbc89290bed7cccf905eef359989275319b331753"),
        h512("4525966a74f9b2ba564fcceee50bf1c4d2faa35a307983df026a18b3486da6ad5bf4b9a56a8d7e35484af"
             "2cafb6224059c4ee332c9ec02bd304c0292c2f9efb8")};
    p2pService->setGroupID2NodeList(groudID2NodeList);

    ///< Read the KeyPair of node from configuration file.
    auto nodePrivate = contents(getDataDir().string() + "/node.private");
    KeyPair key_pair;
    string pri = asString(nodePrivate);
    if (pri.size() >= 64)
    {
        key_pair = KeyPair(Secret(fromHex(pri.substr(0, 64))));
        LOG(INFO) << "Sync Load KeyPair " << toPublic(key_pair.secret());
    }
    else
    {
        LOG(ERROR) << "Sync Load KeyPair Fail! Please Check node.private File.";
        exit(-1);
    }
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
    createTx(txPool, blockChain, params.groupSize(), params.txSpeed(), params.totalTransactions(),
        key_pair);
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);
    startSync(params);
    return 0;
}
