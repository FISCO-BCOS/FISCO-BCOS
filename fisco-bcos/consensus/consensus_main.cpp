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

#include "FakeLedger.h"
#include <fisco-bcos/Fake.h>
#include <fisco-bcos/ParamParse.h>
#include <libconsensus/pbft/PBFTConsensus.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/easylog.h>
#include <libethcore/Protocol.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>

using namespace dev::ledger;

class P2PMessageFactory : public MessageFactory
{
public:
    virtual ~P2PMessageFactory() {}
    virtual Message::Ptr buildMessage() override { return std::make_shared<Message>(); }
};

static void startConsensus(Params& params)
{
    ///< initialize component
    auto p2pMsgHandler = std::make_shared<P2PMsgHandler>();

    std::shared_ptr<AsioInterface> asioInterface = std::make_shared<AsioInterface>();
    std::shared_ptr<NetworkConfig> netConfig = params.creatNetworkConfig();
    std::shared_ptr<SocketFactory> socketFactory = std::make_shared<SocketFactory>();
    std::shared_ptr<SessionFactory> sessionFactory = std::make_shared<SessionFactory>();
    std::shared_ptr<Host> host =
        std::make_shared<Host>(params.clientVersion(), CertificateServer::GetInstance().keypair(),
            *netConfig.get(), asioInterface, socketFactory, sessionFactory);

    host->setStaticNodes(params.staticNodes());
    /// set the topic id
    GroupID group_id = 2;
    PROTOCOL_ID protocol_id = getGroupProtoclID(group_id, ProtocolID::PBFT);
    std::shared_ptr<Service> p2pService = std::make_shared<Service>(host, p2pMsgHandler);
    ///< Read the KeyPair of node from configuration file.
    auto nodePrivate = contents(getDataDir().string() + "/node.private");
    KeyPair key_pair;
    string pri = asString(nodePrivate);
    if (pri.size() >= 64)
    {
        key_pair = KeyPair(Secret(fromHex(pri.substr(0, 64))));
        LOG(INFO) << "Consensus Load KeyPair " << toPublic(key_pair.secret());
    }
    else
    {
        LOG(ERROR) << "Consensus Load KeyPair Fail! Please Check node.private File.";
        exit(-1);
    }
    /// init all the modules through ledger
    std::shared_ptr<LedgerManager<FakeLedger>> ledgerManager =
        std::make_shared<LedgerManager<FakeLedger>>();
    ledgerManager->initSingleLedger(p2pService, group_id, key_pair, "fisco-bcos-data");
    /// start the host
    host->start();
    std::cout << "#### protocol_id:" << protocol_id << std::endl;
    std::shared_ptr<std::vector<std::string>> topics = host->topics();
    topics->push_back(toString(group_id));
    host->setTopics(topics);
    std::map<GROUP_ID, h512s> groudID2NodeList;
    groudID2NodeList[int(group_id)] =
        ledgerManager->getParamByGroupId(group_id)->mutableConsensusParam().minerList;
    p2pService->setGroupID2NodeList(groudID2NodeList);
    /// start all the modules through ledger
    ledgerManager->startAll();
    /// test pbft status
    std::cout << "#### pbft consensus:" << ledgerManager->consensus(group_id)->consensusStatus()
              << std::endl;
    ///< transaction related
    bytes rlpBytes = fromHex(
        "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a100000000000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e000000000000000000000000000000000000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
    Transaction tx(ref(rlpBytes), CheckTransaction::Everything);
    Secret sec = key_pair.secret();
    u256 maxBlockLimit = u256(1000);
    /// get the consensus status

    /// m_txSpeed default is 10
    uint16_t sleep_interval = (uint16_t)(1000.0 / params.txSpeed());
    while (true)
    {
        tx.setNonce(tx.nonce() + u256(1));
        tx.setBlockLimit(u256(ledgerManager->blockChain(group_id)->number()) + maxBlockLimit);
        dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        std::pair<h256, Address> ret = ledgerManager->txPool(group_id)->submit(tx);
        /// LOG(INFO) << "Import tx hash:" << dev::toJS(ret.first)
        ///          << ", size:" << txPool->pendingSize();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
    }
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);

    startConsensus(params);
}
