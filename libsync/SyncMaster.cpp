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
 * @brief : Sync implementation
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#include "SyncMaster.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;

SyncMaster::SyncMaster(BlockChainInterface& _blockChain, TxPoolInterface& _txPool,
    int16_t const _protocolId, unsigned _idleWaitMs = 30)
  : m_blockChain(_blockChain),
    m_txPool(_txPool),
    m_protocolId(_protocolId),
    SyncInterface(_protocolId, _idleWaitMs)
{}

void SyncMaster::start()
{
    startWorking();
}

void SyncMaster::stop()
{
    stopWorking();
}

void SyncMaster::doWork()
{
    if (!isSyncing())
    {
        cout << "SyncMaster " << m_protocolId << " doWork()" << endl;
    }
}

SyncStatus SyncMaster::status() const
{
    RecursiveGuard l(x_sync);
    SyncStatus res;
    res.state = m_state;
    res.protocolId = m_protocolId;
    res.startBlockNumber = m_startingBlock;
    res.currentBlockNumber = m_blockChain.number();
    res.highestBlockNumber = m_highestBlock;
    return res;
}

bool SyncMaster::isSyncing() const
{
    return m_state != SyncState::Idle;
}
