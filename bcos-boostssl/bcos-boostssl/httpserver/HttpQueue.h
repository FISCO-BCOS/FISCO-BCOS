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

namespace bcos::boostssl::http
{
// The queue for http request pipeline
class Queue
{
private:
    // the maximum number size of queue
    std::size_t m_limit;
    // messages to be send
    std::deque<HttpResponsePtr> m_allResp;
    // send handler
    std::function<void(HttpResponsePtr)> m_sender;

public:
    explicit Queue(std::size_t _limit = 16);

    void setSender(std::function<void(HttpResponsePtr)> _sender);
    std::function<void(HttpResponsePtr)> sender() const;

    std::size_t limit() const;
    void setLimit(std::size_t _limit);

    // if the queue reached the m_limit
    bool isFull() const;

    // called when a message finishes sending
    // returns `true` if the caller should initiate a read
    bool onWrite();

    // enqueue and waiting called by the HTTP handler to send a response.
    void enqueue(HttpResponsePtr _msg);
};
}  // namespace bcos::boostssl::http
