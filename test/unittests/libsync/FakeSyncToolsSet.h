/*
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
 */

/**
 * @brief : fake sync tools set
 * @author: catli
 * @date: 2018-10-25
 */

#pragma once
#include <test/unittests/libethcore/FakeBlock.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <memory>

namespace dev
{
namespace test
{
class FakeSyncToolsSet
{
    using TxPoolPtr = std::shared_ptr<dev::txpool::TxPoolInterface>;
    using ServicePtr = std::shared_ptr<dev::p2p::Service>;
    using BlockChainPtr = std::shared_ptr<dev::blockchain::BlockChainInterface>;
    using SessionPtr = std::shared_ptr<dev::p2p::SessionFace>;
    using TransactionPtr = std::shared_ptr<dev::eth::Transaction>;

public:
    FakeSyncToolsSet(uint64_t _blockNum = 5, size_t const& _transSize = 5)
      : m_txPoolFixture(_blockNum, _transSize), m_fakeBlock()
    {
        m_host = createFakeHost(m_clientVersion, m_listenIp, m_listenPort);
    }

    TxPoolPtr getTxPoolPtr() const { return m_txPoolFixture.m_txPool; }

    ServicePtr getServicePtr() const { return m_txPoolFixture.m_topicService; }

    BlockChainPtr getBlockChainPtr() const { return m_txPoolFixture.m_blockChain; }

    Block& getBlock() { return m_fakeBlock.getBlock(); }

    SessionPtr createSession(std::string _ip = "127.0.0.1")
    {
        unsigned const protocolVersion = 0;
        NodeIPEndpoint peer_endpoint(bi::address::from_string(_ip), m_listenPort, m_listenPort);
        KeyPair key_pair = KeyPair::create();
        std::shared_ptr<Peer> peer = std::make_shared<Peer>(key_pair.pub(), peer_endpoint);
        PeerSessionInfo peer_info({key_pair.pub(), peer_endpoint.address.to_string(),
            chrono::steady_clock::duration(), 0});

        std::shared_ptr<SessionFace> session =
            std::make_shared<FakeSessionForHost>(m_host, peer, peer_info);
        session->start();
        return session;
    }

    SessionPtr createSessionWithID(NodeID _peerID, std::string _ip = "127.0.0.1")
    {
        unsigned const protocolVersion = 0;
        NodeIPEndpoint peer_endpoint(bi::address::from_string(_ip), m_listenPort, m_listenPort);
        std::shared_ptr<Peer> peer = std::make_shared<Peer>(_peerID, peer_endpoint);
        PeerSessionInfo peer_info(
            {_peerID, peer_endpoint.address.to_string(), chrono::steady_clock::duration(), 0});

        std::shared_ptr<SessionFace> session =
            std::make_shared<FakeSessionForHost>(m_host, peer, peer_info);
        session->start();
        return session;
    }

    TransactionPtr createTransaction(int64_t _currentBlockNumber)
    {
        const bytes c_txBytes = fromHex(
            "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
            "0000"
            "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
            "0000"
            "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
            "657c"
            "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
        const u256 c_maxBlockLimit = u256(1000);
        Secret sec = KeyPair::create().secret();
        TransactionPtr txPtr =
            std::make_shared<Transaction>(ref(c_txBytes), CheckTransaction::Everything);
        txPtr->setNonce(txPtr->nonce() + u256(rand()));
        txPtr->setBlockLimit(u256(_currentBlockNumber) + c_maxBlockLimit);
        dev::Signature sig = sign(sec, txPtr->sha3(WithoutSignature));
        txPtr->updateSignature(SignatureStruct(sig));
        return txPtr;
    }

private:
    FakeHost* m_host;
    std::string m_clientVersion = "2.0";
    std::string m_listenIp = "127.0.0.1";
    uint16_t m_listenPort = 30304;

    TxPoolFixture m_txPoolFixture;
    FakeBlock m_fakeBlock;
};
}  // namespace test
}  // namespace dev
