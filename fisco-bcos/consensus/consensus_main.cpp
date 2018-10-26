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

#include <fisco-bcos/Fake.h>
#include <fisco-bcos/ParamParse.h>
#include <initializer/Initializer.h>
#include <initializer/LedgerInitiailizer.h>
#include <initializer/P2PInitializer.h>
#include <initializer/SecureInitiailizer.h>
#include <libconsensus/pbft/PBFTConsensus.h>
#include <libdevcore/easylog.h>
#include <libethcore/Protocol.h>
#include <libledger/LedgerManager.h>
#include <libtxpool/TxPool.h>


using namespace dev;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace dev::txpool;
class P2PMessageFactory : public MessageFactory
{
public:
    virtual ~P2PMessageFactory() {}
    virtual Message::Ptr buildMessage() override { return std::make_shared<Message>(); }
};

static void createTx(std::shared_ptr<LedgerManager<FakeLedger>> ledgerManager,
    GROUP_ID const& group_id, float txSpeed, KeyPair const& key_pair)
{
    std::shared_ptr<BlockChainInterface> blockChain = ledgerManager->blockChain(group_id);
    std::shared_ptr<TxPoolInterface> txPool = ledgerManager->txPool(group_id);
    new thread([&]() {
        ///< transaction related
        bytes rlpBytes = fromHex(
            "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
            "0000"
            "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
            "0000"
            "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
            "657c"
            "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
        Transaction tx(ref(rlpBytes), CheckTransaction::Everything);
        Secret sec = key_pair.secret();
        u256 maxBlockLimit = u256(1000);
        /// get the consensus status
        /// m_txSpeed default is 10
        uint16_t sleep_interval = (uint16_t)(1000.0 / txSpeed);
        while (true)
        {
            tx.setNonce(tx.nonce() + u256(1));
            tx.setBlockLimit(u256(blockChain->number()) + maxBlockLimit);
            dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
            tx.updateSignature(SignatureStruct(sig));
            std::pair<h256, Address> ret = txPool->submit(tx);
            /// LOG(INFO) << "Import tx hash:" << dev::toJS(ret.first)
            ///          << ", size:" << txPool->pendingSize();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
        }
    });
}

static void startConsensus(Params& params)
{
    ///< initialize component
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.conf");

    auto p2pInitializer = initialize->p2pInitializer();
    auto p2pService = p2pInitializer->p2pService();
    p2pService->setMessageFactory(std::make_shared<P2PMessageFactory>());
    auto secureInitiailizer = initialize->secureInitiailizer();
    KeyPair key_pair = secureInitiailizer->keyPair();
    auto ledgerManager = initialize->ledgerInitiailizer()->ledgerManager();

    /// create transaction
    for (GROUP_ID i = 1; i <= params.groupSize(); i++)
    {
        createTx(ledgerManager, i, params.txSpeed(), key_pair);
    }
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

int main(int argc, const char* argv[])
{
    Params params = initCommandLine(argc, argv);
    startConsensus(params);
}
