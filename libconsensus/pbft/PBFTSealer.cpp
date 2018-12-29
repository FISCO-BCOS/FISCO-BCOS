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
 * @brief : implementation of PBFT consensus
 * @file: PBFTSealer.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 *
 * @author: yujiechen
 * @file:PBFTSealer.cpp
 * @date: 2018-10-26
 * @modifications: rename PBFTSealer.cpp to PBFTSealer.cpp
 */
#include "PBFTSealer.h"
#include <libdevcore/CommonJS.h>
#include <libdevcore/Worker.h>
#include <libethcore/CommonJS.h>
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::p2p;
namespace dev
{
namespace consensus
{
void PBFTSealer::handleBlock()
{
    setBlock();
    PBFTSEALER_LOG(INFO) << "++++ [#Generating seal on]:  "
                         << "[myIdx/blockNumber/txNum/hash]:  " << m_pbftEngine->nodeIdx() << "/"
                         << m_sealing.block.header().number() << "/"
                         << m_sealing.block.getTransactionSize() << "/"
                         << m_sealing.block.header().hash() << std::endl;
    bool succ = m_pbftEngine->generatePrepare(m_sealing.block);
    if (!succ)
    {
        resetSealingBlock();
        /// notify to re-generate the block
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }
    else if (m_pbftEngine->shouldReset(m_sealing.block))
    {
        resetSealingBlock();
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }
}
void PBFTSealer::setBlock()
{
    resetSealingHeader(m_sealing.block.header());
    m_sealing.block.calTransactionRoot();
}

/**
 * @brief: this node can generate block or not
 * @return true: this node can generate block
 * @return false: this node can't generate block
 */
bool PBFTSealer::shouldSeal()
{
    /// LOG(DEBUG)<<"#### Sealer::shouldSeal():"<<Sealer::shouldSeal();
    /// LOG(DEBUG)<<"#### m_pbftEngine->shouldSeal:"<<m_pbftEngine->shouldSeal();
    return Sealer::shouldSeal() && m_pbftEngine->shouldSeal();
}

/*uint64_t PBFTSealer::calculateMaxPackTxNum()
{
    return m_pbftEngine->calculateMaxPackTxNum(m_maxBlockTransactions);
}*/

void PBFTSealer::start()
{
    m_pbftEngine->start();
    Sealer::start();
}
void PBFTSealer::stop()
{
    Sealer::stop();
    m_pbftEngine->stop();
}
}  // namespace consensus
}  // namespace dev
