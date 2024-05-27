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

#include "bcos-utilities/BoostLog.h"
#define NOMINMAX
#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN32_)
#define _WIN32_WINNT 0x0601
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include "Common.h"
#include "Exceptions.h"
#include <csignal>
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

s256 u2s(u256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    /// get the +/- symbols
    if (boost::multiprecision::bit_test(_u, 255))
        return s256(-(c_end - _u));
    else
        return s256(_u);
}
u256 s2u(s256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    if (_u >= 0)
        return u256(_u);
    else
        return u256(c_end + _u);
}
bool isalNumStr(std::string const& _stringData)
{
    for (auto ch : _stringData)
    {
        if (isalnum(ch))
        {
            continue;
        }
        return false;
    }
    return true;
}
double calcAvgRate(uint64_t _data, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto avgRate = (double)_data * 8 * 1000 / 1024 / 1024 / _intervalMS;
        return avgRate;
    }
    return 0;
}
uint32_t calcAvgQPS(uint64_t _requestCount, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto qps = _requestCount * 1000 / _intervalMS;
        return qps;
    }
    return 0;
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

std::string bcos::pthread_getThreadName()
{
#if defined(__GLIBC__) || defined(__APPLE__)
    std::array<char, 16> name = {0};
    auto err = pthread_getname_np(pthread_self(), (char*)name.data(), name.size());
    if (err == 0)
    {
        if (name[0] == '\0')
        {
            return "";
        }
        return {name.data()};
    }
#endif
    return "";
}
