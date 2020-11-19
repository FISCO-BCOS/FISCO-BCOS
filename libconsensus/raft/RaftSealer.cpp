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
 * @brief : implementation of Raft consensus sealer
 * @file: RaftSealer.cpp
 * @author: catli
 * @date: 2018-12-05
 */
#include "RaftSealer.h"

using namespace std;
using namespace bcos;
using namespace bcos::consensus;


void RaftSealer::start()
{
    m_raftEngine->start();
    Sealer::start();
}

void RaftSealer::stop()
{
    Sealer::stop();
    m_raftEngine->stop();
}

bool RaftSealer::shouldSeal()
{
    return Sealer::shouldSeal() && m_raftEngine->shouldSeal();
}

bool RaftSealer::reachBlockIntervalTime()
{
    return m_raftEngine->reachBlockIntervalTime();
}

void RaftSealer::handleBlock()
{
    resetSealingHeader(m_sealing.block->header());
    m_sealing.block->calTransactionRoot();

    RAFTSEALER_LOG(INFO) << LOG_DESC("[handleBlock]++++++++++++++++ Generating seal")
                         << LOG_KV("blockNumber", m_sealing.block->header().number())
                         << LOG_KV("txNum", m_sealing.block->getTransactionSize())
                         << LOG_KV("hash", m_sealing.block->header().hash().abridged());

    if (m_sealing.block->getTransactionSize() == 0)
    {
        RAFTSEALER_LOG(TRACE) << LOG_DESC("[handleBlock]Empty block will not be committed");
        reset();
        m_raftEngine->resetLastBlockTime();
        return;
    }

    bool succ = m_raftEngine->commit(*(m_sealing.block));
    if (!succ)
    {
        reset();
    }
}
