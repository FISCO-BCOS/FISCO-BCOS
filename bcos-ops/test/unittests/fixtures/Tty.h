/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief makes STDOUT_FILENO a pseudo-terminal for a scope, so wantJson() takes the text path
 * @file Tty.h
 */
#pragma once
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace bcos::ops::test
{
struct ScopedTtyStdout
{
    int saved = ::dup(STDOUT_FILENO);
    int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    std::atomic<bool> stop{false};
    std::thread drain;
    ScopedTtyStdout()
    {
        BOOST_REQUIRE(saved >= 0 && master >= 0);
        BOOST_REQUIRE(::grantpt(master) == 0 && ::unlockpt(master) == 0);
        int slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY);
        BOOST_REQUIRE(slave >= 0);
        std::fflush(stdout);
        ::dup2(slave, STDOUT_FILENO);
        ::close(slave);
        // anything the code under test prints to stdout (Boost.Log's default sink, say) is read
        // off the master, so a full pty buffer can never block the test
        drain = std::thread([this]() {
            char buffer[4096];
            while (!stop.load())
            {
                pollfd ready{master, POLLIN, 0};
                if (::poll(&ready, 1, 20) > 0 && ::read(master, buffer, sizeof(buffer)) <= 0)
                {
                    return;
                }
            }
        });
    }
    ~ScopedTtyStdout()
    {
        std::fflush(stdout);
        ::dup2(saved, STDOUT_FILENO);
        ::close(saved);
        stop.store(true);
        drain.join();
        ::close(master);
    }
    ScopedTtyStdout(ScopedTtyStdout const&) = delete;
    ScopedTtyStdout& operator=(ScopedTtyStdout const&) = delete;
};
}  // namespace bcos::ops::test
