#include <libconsensus/raft/RaftEngine.h>

namespace dev
{
namespace test
{
class FakeRaftEngine : public dev::consensus::RaftEngine
{
public:
    FakeRaftEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _keyPair, unsigned _minElectTime, unsigned _maxElectTime,
        dev::PROTOCOL_ID const& _protocolId, dev::h512s const& _minerList = dev::h512s())
      : RaftEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _keyPair,
            _minElectTime, _maxElectTime, _protocolId, _minerList)
    {}

    void updateMinerList()
    {
        if (m_highestBlockHeader.number() == m_lastObtainMinerNum)
            return;
        m_lastObtainMinerNum = m_highestBlock.number();
    }

    dev::p2p::P2PMessage::Ptr generateVoteReq()
    {
        return dev::consensus::RaftEngine::generateVoteReq();
    }

    dev::p2p::P2PMessage::Ptr generateHeartbeat()
    {
        return dev::consensus::RaftEngine::generateHeartbeat();
    }

    dev::consensus::RaftMsgQueue& msgQueue() { return m_msgQueue; }

    void onRecvRaftMessage(dev::p2p::NetworkException _exception,
        dev::p2p::P2PSession::Ptr _session, dev::p2p::P2PMessage::Ptr _message)
    {
        return dev::consensus::RaftEngine::onRecvRaftMessage(_exception, _session, _message);
    }

    ssize_t getIndexByMiner(dev::h512 const& _nodeId)
    {
        return dev::consensus::RaftEngine::getIndexByMiner(_nodeId);
    }

    bool getNodeIdByIndex(h512& _nodeId, const u256& _nodeIdx) const
    {
        return dev::consensus::RaftEngine::getNodeIdByIndex(_nodeId, _nodeIdx);
    }

    void workLoop() {}
};
}  // namespace test
}  // namespace dev