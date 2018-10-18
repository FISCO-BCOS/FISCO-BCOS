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
 * @brief : The implementation of callback from p2p
 * @author: jimmyshi
 * @date: 2018-10-17
 */

#pragma once
#include "Common.h"
#include "SyncStatus.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Exceptions.h>
#include <libp2p/Common.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/Session.h>
#include <libtxpool/TxPoolInterface.h>

namespace dev
{
namespace sync
{
class SyncMsgEngine
{
public:
    SyncMsgEngine(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<SyncMasterStatus> _data)
      : m_blockChain(_blockChain), m_txPool(_txPool), m_data(_data)
    {}

    void messageHandler(dev::p2p::P2PException _e, std::shared_ptr<dev::p2p::Session> _session,
        dev::p2p::Message::Ptr _msg);

private:
    bool checkSession(std::shared_ptr<dev::p2p::Session> _session);
    bool checkPacket(bytesConstRef _msg);
    bool interpret(NodeID const& _id, SyncPacketType _type, RLP const& _r);

private:
    void onPeerTransactions(NodeID const& _id, RLP const& _r);

private:
    // Outside data
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::shared_ptr<SyncMasterStatus> m_data;
};

}  // namespace sync
}  // namespace dev
