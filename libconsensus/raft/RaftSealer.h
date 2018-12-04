#pragma once
#include "Common.h"
#include "RaftEngine.h"
#include <libconsensus/Sealer.h>
#include <memory>

namespace dev
{
namespace consensus
{
class RaftSealer : public Sealer
{
public:
    RaftSealer(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _keyPair, unsigned _minElectTime, unsigned _maxElectTime,
        dev::PROTOCOL_ID const& _protocolId, dev::h512s const& _minerList = dev::h512s())
      : Sealer(_txPool, _blockChain, _blockSync)
    {
        m_consensusEngine = std::make_shared<RaftEngine>(_service, _txPool, _blockChain, _blockSync,
            _blockVerifier, _keyPair, _minElectTime, _maxElectTime, _protocolId, _minerList);
        m_raftEngine = std::dynamic_pointer_cast<RaftEngine>(m_consensusEngine);
    }
    void start() override;
    void stop() override;

protected:
    void handleBlock() override;
    bool shouldSeal() override;
    bool reachBlockIntervalTime() override;

private:
    std::shared_ptr<RaftEngine> m_raftEngine;
};
}  // namespace consensus
}  // namespace dev