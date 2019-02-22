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
 * @brief : header file of tools for thread synchronization
 * @file: ParaTxExecutor.h
 * @author: catli
 * @date: 2018-02-21
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <string>

namespace dev
{
class CountDownLatch
{
public:
    CountDownLatch(int _count) : m_count(_count) {}
    CountDownLatch(const CountDownLatch&) = delete;

    void wait()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        while (m_count > 0)
        {
            m_cv.wait(ul);
        }
    }

    void countDown()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        --m_count;
        if (m_count == 0)
        {
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
};

class Notifier
{
public:
    Notifier() : m_ready(false) {}
    Notifier(const Notifier&) = delete;

    void wait()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        while (!m_ready)
        {
            m_cv.wait(ul);
        }
        m_ready = false;
    }

    void notify()
    {
        std::unique_lock<std::mutex> ul(m_mutex);
        m_ready = true;
        m_cv.notify_all();
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

class SpinLock
{
public:
    SpinLock() {}

    void lock()
    {
        while (m_flag.test_and_set(std::memory_order_acquire))
        {
        }
    }

    void unlock() { m_flag.clear(std::memory_order_release); }

private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};
}  // namespace dev