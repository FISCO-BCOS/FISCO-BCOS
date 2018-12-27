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
 * @brief : implementation for the base of the PBFT consensus
 * @file: ConsensusEngineBase.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "Common.h"
#include "ConsensusInterface.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Block.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/P2PSession.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>

namespace dev
{
namespace consensus
{
class ConsensusEngineBase : public Worker, virtual public ConsensusInterface
{
public:
    ConsensusEngineBase(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, dev::h512s const& _minerList = dev::h512s())
      : Worker("ConsensusEngineBase", 0),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockSync(_blockSync),
        m_blockVerifier(_blockVerifier),
        m_consensusBlockNumber(0),
        m_protocolId(_protocolId),
        m_minerList(_minerList)
    {
        assert(m_service && m_txPool && m_blockChain && m_blockSync && m_blockVerifier);
        if (m_protocolId == 0)
            BOOST_THROW_EXCEPTION(dev::eth::InvalidProtocolID()
                                  << errinfo_comment("Protocol id must be larger than 0"));
        m_groupId = dev::eth::getGroupAndProtocol(m_protocolId).first;
    }

    void start() override;
    void stop() override;
    virtual ~ConsensusEngineBase() { stop(); }

    /// get miner list
    dev::h512s minerList() const override
    {
        ReadGuard l(m_minerListMutex);
        return m_minerList;
    }
    /// append miner
    void appendMiner(h512 const& _miner) override
    {
        {
            WriteGuard l(m_minerListMutex);
            m_minerList.push_back(_miner);
        }
        resetConfig();
    }

    const std::string consensusStatus() const override
    {
        json_spirit::Object status_obj;
        getBasicConsensusStatus(status_obj);
        json_spirit::Value value(status_obj);
        std::string status_str = json_spirit::write_string(value, true);
        return status_str;
    }
    /// get status of consensus
    void getBasicConsensusStatus(json_spirit::Object& status_obj) const
    {
        status_obj.push_back(json_spirit::Pair("nodeNum", m_nodeNum));
        status_obj.push_back(json_spirit::Pair("node index", m_idx));
        status_obj.push_back(json_spirit::Pair("max_faulty_leader", m_f));
        status_obj.push_back(json_spirit::Pair("consensusedBlockNumber", m_consensusBlockNumber));
        status_obj.push_back(json_spirit::Pair("highestblockNumber", m_highestBlock.number()));
        status_obj.push_back(json_spirit::Pair("highestblockHash", toHex(m_highestBlock.hash())));
        status_obj.push_back(json_spirit::Pair("groupId", m_groupId));
        status_obj.push_back(json_spirit::Pair("protocolId", m_protocolId));
        status_obj.push_back(json_spirit::Pair("accountType", m_accountType));
        int i = 0;
        std::string miner_list = "";
        {
            ReadGuard l(m_minerListMutex);
            for (auto miner : m_minerList)
            {
                status_obj.push_back(json_spirit::Pair("miner." + toString(i), toHex(miner)));
                i++;
            }
        }
        status_obj.push_back(json_spirit::Pair("allowFutureBlocks", m_allowFutureBlocks));
    }

    /// protocol id used when register handler to p2p module
    PROTOCOL_ID const& protocolId() const override { return m_protocolId; }
    GROUP_ID groupId() const override { return m_groupId; }
    /// get account type
    ///@return NodeAccountType::MinerAccount: the node can generate and execute block
    ///@return NodeAccountType::ObserveAccout: the node can only sync block from other nodes
    NodeAccountType accountType() override { return m_accountType; }
    /// set the node account type
    void setNodeAccountType(dev::consensus::NodeAccountType const& _accountType) override
    {
        m_accountType = _accountType;
    }
    /// get the node index if the node is a miner
    IDXTYPE nodeIdx() const override { return m_idx; }

    bool const& allowFutureBlocks() const { return m_allowFutureBlocks; }
    void setAllowFutureBlocks(bool isAllowFutureBlocks)
    {
        m_allowFutureBlocks = isAllowFutureBlocks;
    }

    IDXTYPE minValidNodes() const { return m_nodeNum - m_f; }
    /// update the context of PBFT after commit a block into the block-chain
    virtual void reportBlock(dev::eth::Block const& block) {}

protected:
    virtual void resetConfig() { m_nodeNum = m_minerList.size(); }
    void dropHandledTransactions(dev::eth::Block const& block) { m_txPool->dropBlockTrans(block); }
    /// get the node id of specified miner according to its index
    /// @param index: the index of the node
    /// @return h512(): the node is not in the miner list
    /// @return node id: the node id of the node
    inline h512 getMinerByIndex(size_t const& index) const
    {
        if (index < m_minerList.size())
            return m_minerList[index];
        return h512();
    }

    virtual bool isValidReq(dev::p2p::P2PMessage::Ptr message,
        std::shared_ptr<dev::p2p::P2PSession> session, ssize_t& peerIndex)
    {
        return false;
    }
    /**
     * @brief: decode the network-received message to corresponding message(PBFTMsgPacket)
     *
     * @tparam T: the type of corresponding message
     * @param req : constructed object from network-received message
     * @param message : network-received(received from service of p2p module)
     * @param session : session related with the received-message(can obtain information of
     * sender-peer)
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(
        T& req, dev::p2p::P2PMessage::Ptr message, std::shared_ptr<dev::p2p::P2PSession> session)
    {
        ssize_t peer_index = 0;
        bool valid = isValidReq(message, session, peer_index);
        if (valid)
        {
            valid = decodeToRequests(req, ref(*(message->buffer())));
            if (valid)
                req.setOtherField(
                    peer_index, session->nodeID(), session->session()->nodeIPEndpoint().name());
        }
        return valid;
    }

    /**
     * @brief : decode the given data to object
     * @tparam T : type of the object obtained from the given data
     * @param req : the object obtained from the given data
     * @param data : data need to be decoded into object
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(T& req, bytesConstRef data)
    {
        try
        {
            req.decode(data);
            return true;
        }
        catch (std::exception& e)
        {
            ENGINE_LOG(DEBUG) << "[#decodeToRequests] Invalid network-received packet";
            return false;
        }
    }

    dev::blockverifier::ExecutiveContext::Ptr executeBlock(dev::eth::Block& block);
    virtual void checkBlockValid(dev::eth::Block const& block);

    virtual void updateConsensusNodeList();
    virtual void updateNodeListInP2P();

private:
    bool blockExists(h256 const& blockHash)
    {
        if (m_blockChain->getBlockByHash(blockHash) == nullptr)
            return false;
        return true;
    }

protected:
    /// p2p service handler
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// handler of the block-sync module
    std::shared_ptr<dev::sync::SyncInterface> m_blockSync;
    /// handler of the block-verifier module
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;

    // the block which is waiting consensus
    int64_t m_consensusBlockNumber;
    /// the latest block header
    dev::eth::BlockHeader m_highestBlock;
    /// total number of nodes
    IDXTYPE m_nodeNum = 0;
    /// at-least number of valid nodes
    IDXTYPE m_f = 0;

    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    /// type of this node (MinerAccount or ObserveAccount)
    NodeAccountType m_accountType;
    /// index of this node
    IDXTYPE m_idx = 0;
    /// miner list
    mutable SharedMutex m_minerListMutex;
    dev::h512s m_minerList;
    /// allow future blocks or not
    bool m_allowFutureBlocks = true;
    bool m_startConsensusEngine = false;

    /// node list record when P2P last update
    std::string m_lastNodeList;
};
}  // namespace consensus
}  // namespace dev
