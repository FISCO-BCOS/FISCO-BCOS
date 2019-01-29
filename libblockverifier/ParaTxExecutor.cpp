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
 * @brief : implementation of parallel transactions executor
 * @file: ParaTxExecutor.cpp
 * @author: catli
 * @date: 2018-01-09
 */
#include <libblockverifier/ParaTxExecutor.h>
#include <cassert>
#include <mutex>

using namespace dev;
using namespace dev::blockverifier;
using namespace std;

void ParaTxWorker::doWork()
{
    if (m_txDAG == nullptr || m_countDownLatch == nullptr)
    {
        return;
    }

    while (!m_txDAG->hasFinished())
    {
        m_txDAG->executeUnit();
    }

    m_txDAG.reset();
    m_countDownLatch->countDown();
    m_wakeupNotifier.reset();
}

void ParaTxWorker::workLoop()
{
    while (workerState() == WorkerState::Started)
    {
        m_wakeupNotifier.wait();
        doWork();
    }
}

void ParaTxExecutor::initialize(unsigned _threadNum)
{
    m_workers.reserve(_threadNum);
    for (auto i = _threadNum; i > 0; --i)
    {
        m_notifiers.push_back(make_shared<WakeupNotifier>());
        m_workers.push_back(ParaTxWorker(*(m_notifiers.back()), "ParaTxExecutor_" + std::to_string(i)));
        m_workers.back().start();
    }
}

void ParaTxExecutor::start(shared_ptr<TxDAGFace> _txDAG)
{
    auto countDownLatch = make_shared<CountDownLatch>(threadNum());

    for(unsigned int i = 0; i < threadNum(); ++i)
    {
        m_workers[i].setDAG(_txDAG);
        m_workers[i].setCountDownLatch(countDownLatch);
        m_notifiers[i]->notify();
    }

    while (!_txDAG->hasFinished())
    {
        _txDAG->executeUnit();
    }

    countDownLatch->wait();
}
