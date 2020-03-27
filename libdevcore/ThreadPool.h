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
 * @brief: threadpool obtained from libchannelserver/ThreadPool.h of FISCO-BCOS
 *
 * @file ThreadPool.h
 * @author: yujiechen
 * @date 2018-10-20
 */

#pragma once
#include "Common.h"
#include "Log.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <iosfwd>
#include <memory>

namespace dev
{
class ThreadPool
{
public:
    typedef std::shared_ptr<ThreadPool> Ptr;

    explicit ThreadPool(const std::string& threadName, size_t size) : m_work(_ioService)
    {
        _threadName = threadName;
        std::shared_ptr<std::vector<std::string>> fields =
            std::make_shared<std::vector<std::string>>();
        if (boost::log::core::get())
        {
            boost::split(*fields, _threadName, boost::is_any_of("-"), boost::token_compress_on);
        }

        for (size_t i = 0; i < size; ++i)
        {
            _workers.create_thread([this, fields] {
                if (fields->size() > 1)
                {
                    boost::log::core::get()->add_thread_attribute(
                        "GroupId", boost::log::attributes::constant<std::string>((*fields)[1]));
                }
                dev::pthread_setThreadName(_threadName);
                _ioService.run();
            });
        }
    }
    void stop()
    {
        _ioService.stop();
        if (!_workers.is_this_thread_in())
        {
            _workers.join_all();
        }
    }

    ~ThreadPool() { stop(); }

    // Add new work item to the pool.
    template <class F>
    void enqueue(F f)
    {
        _ioService.post(f);
    }

private:
    std::string _threadName;
    boost::thread_group _workers;
    boost::asio::io_service _ioService;
    // m_work ensures that io_service's run() function will not exit while work is underway
    boost::asio::io_service::work m_work;
};

}  // namespace dev
