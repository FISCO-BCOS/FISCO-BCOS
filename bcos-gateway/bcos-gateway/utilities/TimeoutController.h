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
 *  limitations under the License.
 *
 * @file TimeoutController.h
 * @author: chuwen
 * @date 2023-05-15
 */

#pragma once

#include <bcos-gateway/Common.h>
#include <bcos-utilities/Worker.h>

namespace bcos
{

namespace gateway
{

class TimeoutController : public Worker
{
public:
    using Ptr = std::shared_ptr<TimeoutController>;

    TimeoutController();

public:
    void start();

    void stop();

    void addTimeoutPoint(
          const std::string& _p2pID, const int _moduleID, const std::uint64_t _timestamp);

    std::uint32_t getTimeout(const std::string& _p2pID, const int _moduleID);

    void removeP2pID(const std::string& _p2pID);

protected:
    void executeWorker() override;

private:
    bcos::Mutex x_running;
    bool m_running{false};

    bcos::Mutex x_nodeTimeoutPoints;
    using ModuleTimeoutPoints = std::unordered_map<int, std::deque<std::uint64_t>>;
    std::unordered_map<std::string, ModuleTimeoutPoints> m_nodeTimeoutPoints;

    bcos::Mutex x_nodePacketTimeout;
    using ModuleTimeout = std::unordered_map<int, std::uint32_t>;
    std::unordered_map<std::string, ModuleTimeout> m_nodePacketTimeout;
};

}

}




