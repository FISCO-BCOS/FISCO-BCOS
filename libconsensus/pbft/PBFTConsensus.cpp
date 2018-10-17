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
 * @file: PBFTConsensus.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 */
#include "PBFTConsensus.h"
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
void PBFTConsensus::handleBlock()
{
    setBlock();
    LOG(INFO) << "+++++++++++++++++++++++++++ Generating seal on: "
              << m_sealing.block.header().hash()
              << "#block_number:" << m_sealing.block.header().number()
              << "#tx:" << m_sealing.block.getTransactionSize() << "time:" << utcTime();
    m_pbftEngine->generatePrepare(m_sealing.block);
}

void PBFTConsensus::setBlock()
{
    resetSealingHeader(m_sealing.block.header());
    setSealingRoot(m_sealing.block.getTransactionRoot(), h256(), h256());
}
/**
 * @brief: this node can generate block or not
 * @return true: this node can generate block
 * @return false: this node can't generate block
 */
bool PBFTConsensus::shouldSeal()
{
    return Consensus::shouldSeal() && m_pbftEngine->shouldSeal();
}

uint64_t PBFTConsensus::calculateMaxPackTxNum()
{
    return m_pbftEngine->calculateMaxPackTxNum(m_maxBlockTransactions);
}

void PBFTConsensus::start()
{
    Consensus::start();
    m_pbftEngine->start();
}
void PBFTConsensus::stop()
{
    Consensus::stop();
    m_pbftEngine->stop();
}

}  // namespace consensus
}  // namespace dev
