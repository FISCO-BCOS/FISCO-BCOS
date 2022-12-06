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
 * @file Common.cpp
 * @author Asherli
 * @date 2021-02-24
 */

#define NOMINMAX

#include "Common.h"
#include "Exceptions.h"
#include <csignal>
#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN32_)
#include <windows.h>
#else
#include <sys/time.h>
#endif
#ifdef __APPLE__
#include <pthread.h>
#endif

using namespace std;

namespace bcos
{
bytes const NullBytes;
/// get utc time(ms)
uint64_t utcTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// getSteadyTime(ms)
uint64_t utcSteadyTime()
{
    // trans (ns) into (ms)
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// get utc time(us)
uint64_t utcTimeUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

uint64_t utcSteadyTimeUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string getCurrentDateTime()
{
    using std::chrono::system_clock;
    char buffer[40];
    auto currentTime = system_clock::to_time_t(system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    return std::string(buffer);
}

void errorExit(std::stringstream& _exitInfo, Exception const& _exception)
{
    BCOS_LOG(WARNING) << _exitInfo.str();
    std::cerr << _exitInfo.str();
    raise(SIGTERM);
    BOOST_THROW_EXCEPTION(_exception << errinfo_comment(_exitInfo.str()));
}

}  // namespace bcos

void bcos::pthread_setThreadName(std::string const& _n)
{
#if defined(__GLIBC__)
    pthread_setname_np(pthread_self(), _n.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(_n.c_str());
#endif
}
