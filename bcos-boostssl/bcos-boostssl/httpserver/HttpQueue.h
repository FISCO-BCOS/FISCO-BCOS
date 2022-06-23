/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  m_limitations under the License.
 *
 * @file HttpQueue.h
 * @author: octopus
 * @date 2021-07-08
 */
#pragma once
#include <bcos-boostssl/httpserver/Common.h>

namespace bcos
{
namespace boostssl
{
namespace http
{
// The queue for http request pipeline
class Queue
{
private:
    // the maximum number size of queue
    std::size_t m_limit;
    // messages to be send
    std::vector<HttpResponsePtr> m_allResp;
    // send handler
    std::function<void(HttpResponsePtr)> m_sender;

public:
    explicit Queue(std::size_t _limit = 16) : m_limit(_limit) { m_allResp.reserve(m_limit); }

    void setSender(std::function<void(HttpResponsePtr)> _sender) { m_sender = _sender; }
    std::function<void(HttpResponsePtr)> sender() const { return m_sender; }

    std::size_t limit() const { return m_limit; }
    void setLimit(std::size_t _limit) { m_limit = _limit; }

    // if the queue reached the m_limit
    bool isFull() const { return m_allResp.size() >= m_limit; }

    // called when a message finishes sending
    // returns `true` if the caller should initiate a read
    bool onWrite()
    {
        BOOST_ASSERT(!m_allResp.empty());
        auto const was_full = isFull();
        m_allResp.erase(m_allResp.begin());
        if (!m_allResp.empty())
        {
            m_sender(m_allResp.front());
        }
        return was_full;
    }

    // enqueue and waiting called by the HTTP handler to send a response.
    void enqueue(HttpResponsePtr _msg)
    {
        m_allResp.push_back(_msg);
        // there was no previous work, start this one
        if (m_allResp.size() == 1)
        {
            m_sender(m_allResp.front());
        }
    }
};
}  // namespace http
}  // namespace boostssl
}  // namespace bcos
