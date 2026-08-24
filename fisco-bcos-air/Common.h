/**
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
 * @file Common.h
 * @author: yujiechen
 * @date 2021-06-11
 */

#pragma once
#include "bcos-utilities/Common.h"
// clang-format off
#include <csignal>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <boost/atomic.hpp>
// clang-format on


namespace bcos::node
{
class ExitHandler
{
public:
    static void exit()
    {
        // Normal (non-signal) exit path — do NOT chain-call old handlers
        // because signal number 0 is not a real signal and can trigger
        // undefined behaviour in the previously installed handler.
        const char* msg = "[ExitHandler] normal exit requested...\n";
        [[maybe_unused]] auto _ret = write(STDERR_FILENO, msg, sizeof(msg) - 1);
        ExitHandler::c_shouldExit.store(true);
        ExitHandler::c_shouldExit.notify_all();
    }

    /// Signal handler: sets the exit flag first so that main() can proceed,
    /// then chains to the previously registered handler (if any) so that
    /// sub-components like the TARS RPC framework also receive the signal.
    static void exitHandler(int signal)
    {
        // Write a brief message using write() rather than std::cout
        // because std::cout is not async-signal-safe and could deadlock
        // if the logging / IO threads are stuck.
        const char* msg = "[ExitHandler] received signal, exiting...\n";
        // write() is tagged warn_unused_result on Linux; a void-cast does
        // not suppress the warning, but a real use (assignment) does.
        [[maybe_unused]] auto _ret = write(STDERR_FILENO, msg, sizeof(msg) - 1);

        // Set the flag first — this is the critical path that unblocks main().
        ExitHandler::c_shouldExit.store(true);
        ExitHandler::c_shouldExit.notify_all();

        // Chain to the previously registered handler so that frameworks
        // such as TARS can perform their own internal graceful shutdown.
        auto& old = getOldHandler(signal);
        if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction != nullptr)
        {
            old.sa_sigaction(signal, nullptr, nullptr);
        }
        else if (old.sa_handler != nullptr && old.sa_handler != SIG_DFL &&
                 old.sa_handler != SIG_IGN && old.sa_handler != &ExitHandler::exitHandler)
        {
            old.sa_handler(signal);
        }
    }

    /// Register (or re-register) our signal handler, saving the previously
    /// installed handler so that exitHandler() can chain-call it.
    ///
    /// Call once BEFORE Initializer::start() so that crashes during start-up
    /// are caught.  Call again AFTER start() because sub-components (TARS
    /// RPC, etc.) may have installed their own handlers in the meantime.
    static void registerSignalHandlers()
    {
        // clang-format off
        struct sigaction sa{};
        // clang-format on
        sa.sa_handler = &ExitHandler::exitHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;

        sigaction(SIGTERM, &sa, &s_oldTERM);
        sigaction(SIGABRT, &sa, &s_oldABRT);
        sigaction(SIGINT, &sa, &s_oldINT);
        sigaction(SIGSEGV, &sa, &s_oldSEGV);
    }

    static bool shouldExit() { return ExitHandler::c_shouldExit.load(); }

    static boost::atomic_bool c_shouldExit;

private:
    static struct sigaction& getOldHandler(int signal)
    {
        switch (signal)
        {
        case SIGTERM:
            return s_oldTERM;
        case SIGINT:
            return s_oldINT;
        case SIGABRT:
            return s_oldABRT;
        case SIGSEGV:
            return s_oldSEGV;
        default:
            return s_oldTERM;  // fallback
        }
    }

    static struct sigaction s_oldTERM;
    static struct sigaction s_oldINT;
    static struct sigaction s_oldABRT;
    static struct sigaction s_oldSEGV;
};

// setDefaultOrCLocale() is defined in Common.cpp.
void setDefaultOrCLocale();
}  // namespace bcos::node
