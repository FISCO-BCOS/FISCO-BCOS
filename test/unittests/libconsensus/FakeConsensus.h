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
 * @brief:
 * @file: FakeConsensus.cpp
 * @author: yujiechen
 * @date: 2018-10-10
 */
#pragma once
#include <libconsensus/Consensus.h>
#include <libconsensus/pbft/PBFTConsensus.h>
#include <test/unittests/libblocksync/FakeBlockSync.h>
#include <test/unittests/libblockverifier/FakeBlockVerifier.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <memory>
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::txpool;
using namespace dev::blockchain;
using namespace dev::consensus;

namespace dev
{
namespace test
{
class FakePBFTConsensus : public PBFTConsensus
{
public:
    FakePBFTConsensus(std::shared_ptr<dev::p2p::Service> _service,
        std::shared_ptr<dev::txpool::TxPool> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, h512s const& _minerList = h512s(),
        std::string const& _baseDir = "./", KeyPair const& _key_pair = KeyPair::create())
      : PBFTConsensus(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _baseDir, _key_pair, _minerList)
    {}

    KeyPair const& keyPair() const { return m_keyPair; }
    const std::shared_ptr<PBFTBroadcastCache> broadCastCache() const { return m_broadCastCache; }
    const std::shared_ptr<PBFTReqCache> reqCache() const { return m_reqCache; }
    TimeManager const& timeManager() const { return m_timeManager; }
    const std::shared_ptr<dev::db::LevelDB> backupDB() const { return m_backupDB; }
    bool const& leaderFailed() const { return m_leaderFailed; }
    int64_t const& consensusBlockNumber() const { return m_consensusBlockNumber; }
    u256 const& toView() const { return m_toView; }
    u256 const& view() const { return m_view; }

    bool isDiskSpaceEnough(std::string const& path) override
    {
        std::size_t pos = path.find("invalid");
        if (pos != std::string::npos)
            return false;
        return boost::filesystem::space(path).available > 1024;
    }

    void resetConfig() { PBFTConsensus::resetConfig(); }
    PBFTMsgQueue& mutableMsgQueue() { return m_msgQueue; }
    void onRecvPBFTMessage(
        P2PException exception, std::shared_ptr<Session> session, Message::Ptr message)
    {
        return PBFTConsensus::onRecvPBFTMessage(exception, session, message);
    }

    Message::Ptr transDataToMessage(
        bytesConstRef data, uint16_t const& packetType, uint16_t const& protocolId)
    {
        return PBFTConsensus::transDataToMessage(data, packetType, protocolId);
    }

    void broadcastMsg(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<h512> const& filter = std::unordered_set<h512>())
    {
        return PBFTConsensus::broadcastMsg(packetType, key, data, filter);
    }

    bool broadcastFilter(h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return PBFTConsensus::broadcastFilter(nodeId, packetType, key);
    }
    std::shared_ptr<P2PInterface> mutableService() { return m_service; }
    std::shared_ptr<BlockChainInterface> blockChain() { return m_blockChain; }
    void broadcastSignReq(PrepareReq const& req) { return PBFTConsensus::broadcastSignReq(req); }
    u256 view() { return m_view; }
    void setView(u256 const& _view) { m_view = _view; }
    void checkAndSave() { return PBFTConsensus::checkAndSave(); }

    void setHighest(BlockHeader const& header) { m_highestBlock = header; }
    BlockHeader& mutableHighest() { return m_highestBlock; }
    void setNodeNum(u256 const& nodeNum) { m_nodeNum = nodeNum; }
    u256 const& nodeNum() { return m_nodeNum; }
    u256 const& fValue() { return m_f; }
    void setF(u256 const& fValue) { m_f = fValue; }
    int64_t const& consensusBlockNumber() { return m_consensusBlockNumber; }
    bool const& leaderFailed() { return m_leaderFailed; }
    bool const& cfgErr() { return m_cfgErr; }
    void resetBlock(dev::eth::Block& block) { return PBFTConsensus::resetBlock(block); }
    void initPBFTEnv(unsigned _view_timeout) { return PBFTConsensus::initPBFTEnv(_view_timeout); }
};

template <typename T>
class FakeConsensus
{
public:
    FakeConsensus(size_t minerSize, int16_t protocolID)
    {
        /// fake blocksync
        std::shared_ptr<SyncInterface> m_sync = std::make_shared<FakeBlockSync>();
        /// fake blockverifier
        std::shared_ptr<BlockVerifierInterface> m_blockVerifier =
            std::make_shared<FakeBlockverifier>();
        TxPoolFixture txpool_creator(5, 5);
        /// fake minerList
        FakeMinerList(minerSize);
        m_consensus = std::make_shared<T>(txpool_creator.m_topicService, txpool_creator.m_txPool,
            txpool_creator.m_blockChain, m_sync, m_blockVerifier, protocolID, m_minerList);
    }

    /// fake miner list
    void FakeMinerList(size_t minerSize)
    {
        m_minerList.clear();
        for (size_t i = 0; i < minerSize; i++)
        {
            KeyPair key_pair = KeyPair::create();
            m_minerList.push_back(key_pair.pub());
        }
    }
    std::shared_ptr<T> consensus() { return m_consensus; }

public:
    h512s m_minerList;

private:
    std::shared_ptr<T> m_consensus;
};
}  // namespace test
}  // namespace dev
