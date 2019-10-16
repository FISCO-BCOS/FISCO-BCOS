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
 * @brief : Downloading transactions queue
 * @author: jimmyshi
 * @date: 2019-02-19
 */

#pragma once
#include "Common.h"

#include <libdevcore/Guards.h>
#include <libethcore/Transaction.h>
#include <libethcore/TxsParallelParser.h>
#include <libp2p/StatisticHandler.h>
#include <libtxpool/TxPoolInterface.h>
#include <vector>

namespace dev
{
namespace sync
{
class DownloadTxsShard
{
public:
    DownloadTxsShard(bytesConstRef _txsBytes, NodeID const& _fromPeer)
      : txsBytes(_txsBytes.toBytes()), fromPeer(_fromPeer)
    {}
    bytes txsBytes;
    NodeID fromPeer;
};

class DownloadingTxsQueue
{
public:
    DownloadingTxsQueue(PROTOCOL_ID const&, NodeID const& _nodeId)
      : m_nodeId(_nodeId), m_buffer(std::make_shared<std::vector<DownloadTxsShard>>())
    {}
    // push txs bytes in queue
    void push(bytesConstRef _txsBytes, NodeID const& _fromPeer);

    // pop all queue into tx pool
    void pop2TxPool(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        dev::eth::CheckTransaction _checkSig = dev::eth::CheckTransaction::None);

    ssize_t bufferSize() const
    {
        ReadGuard l(x_buffer);
        return m_buffer->size();
    }

    void setStatisticHandler(dev::p2p::StatisticHandler::Ptr _statisticHandler)
    {
        m_statisticHandler = _statisticHandler;
    }

private:
    NodeID m_nodeId;
    std::shared_ptr<std::vector<DownloadTxsShard>> m_buffer;
    mutable SharedMutex x_buffer;

    dev::p2p::StatisticHandler::Ptr m_statisticHandler = nullptr;
};

}  // namespace sync
}  // namespace dev