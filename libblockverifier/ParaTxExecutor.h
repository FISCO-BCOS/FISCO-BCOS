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
 * @brief : header file of parallel transactions executor
 * @file: ParaTxExecutor.h
 * @author: catli
 * @date: 2018-01-09
 */
#pragma once

#include <libblockverifier/Common.h>
#include <libblockverifier/TxDAG.h>
#include <libdevcore/Worker.h>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define PARA_TX_EXECUTOR_LOG(LEVEL) BLOCKVERIFIER_LOG(LEVEL) << LOG_BADGE("PARATXEXECUTOR")

namespace dev
{
namespace blockverifier
{
class CountDownLatch
{
public:
    CountDownLatch(int _count) : m_count(_count), m_init(_count) {}
    CountDownLatch(const CountDownLatch&) = delete;

    void wait()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        m_cv.wait(ul, [this]() { return m_count == 0; });
    }

    void countDown()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        --m_count;
        if (m_count == 0)
        {
            ul.unlock();
            m_cv.notify_all();
        }
    }

    int getCount()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        return m_count;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_count;
    int m_init;
};

class WakeupNotifier
{
public:
    WakeupNotifier() : m_ready(false) {}
    WakeupNotifier(const WakeupNotifier&) = delete;

    void wait()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        m_cv.wait(ul, [this]() { return m_ready == true; });
    }

    void notify()
    {
        {
            std::unique_lock<std::mutex> ul(m_mutex);
            m_ready = true;
        }
        m_cv.notify_all();
    }

    void reset()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        m_ready = false;
    }

    bool isReady()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        return m_ready;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_ready;
};

class ParaTxWorker : dev::Worker
{
public:
    ParaTxWorker(WakeupNotifier& _wakeupNotifier, std::string const& _name = "anon",
        unsigned _idleWaitMs = 30)
      : dev::Worker(_name, _idleWaitMs),
        m_txDAG(nullptr),
        m_wakeupNotifier(_wakeupNotifier),
        m_countDownLatch(nullptr)
    {}
    void setDAG(std::shared_ptr<TxDAGFace> _txDAG) { m_txDAG = _txDAG; }
    void setCountDownLatch(std::shared_ptr<CountDownLatch> _latch) { m_countDownLatch = _latch; }
    void start() { startWorking(); }

protected:
    void workLoop() override;
    void doWork() override;

private:
    std::shared_ptr<TxDAGFace> m_txDAG;
    WakeupNotifier& m_wakeupNotifier;
    std::shared_ptr<CountDownLatch> m_countDownLatch;
};

class ParaTxExecutor
{
public:
    ParaTxExecutor() {}
    // Initialize thread pool when fisco-bcos process started
    void initialize(unsigned _threadNum = std::thread::hardware_concurrency() - 1);
    // Start to execute DAG, block the calller until the execution of DAG is finished
    void start(std::shared_ptr<TxDAGFace> _txDAG);
    // Count of the worker threads;
    unsigned threadNum() { return m_workers.size(); }

private:
    std::vector<ParaTxWorker> m_workers;
    std::vector<std::shared_ptr<WakeupNotifier>> m_notifiers;
};
}  // namespace blockverifier
}  // namespace dev