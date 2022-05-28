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
#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN32_)
    return std::chrono::steady_clock::now().time_since_epoch().count() / 1000000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

// getSteadyTime(ms)
uint64_t utcSteadyTime()
{
    // trans (ns) into (ms)
    return std::chrono::steady_clock::now().time_since_epoch().count() / 1000000;
}

/// get utc time(us)
uint64_t utcTimeUs()
{
#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN32_)
    return std::chrono::steady_clock::now().time_since_epoch().count() / 1000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

uint64_t utcSteadyTimeUs()
{
    return std::chrono::steady_clock::now().time_since_epoch().count() / 1000;
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

thread_local std::string TimeRecorder::m_name;
thread_local std::chrono::steady_clock::time_point TimeRecorder::m_timePoint;
thread_local size_t TimeRecorder::m_heapCount = 0;
thread_local std::vector<std::pair<std::string, std::chrono::steady_clock::time_point> >
    TimeRecorder::m_record;

TimeRecorder::TimeRecorder(const std::string& function, const std::string& name)
  : m_function(function)
{
    auto now = std::chrono::steady_clock::now();
    if (m_timePoint == std::chrono::steady_clock::time_point())
    {
        m_name = name;
        m_timePoint = now;
    }
    else
    {
        m_record.push_back(std::make_pair(m_name, m_timePoint));

        m_name = name;
        m_timePoint = now;
    }

    ++m_heapCount;
}

TimeRecorder::~TimeRecorder()
{
    --m_heapCount;

    if (!m_heapCount && m_timePoint != std::chrono::steady_clock::time_point())
    {
        auto now = std::chrono::steady_clock::now();
        auto end = now;
        m_record.push_back(std::make_pair(m_name, m_timePoint));

        std::vector<std::chrono::duration<double> > elapseds;
        elapseds.resize(m_record.size());
        std::stringstream ss;
        for (auto i = m_record.size(); i > 0; --i)
        {
            std::chrono::duration<double> elapsed = now - m_record[i - 1].second;
            now = m_record[i - 1].second;

            elapseds[i - 1] = elapsed;
        }

        for (size_t i = 0; i < m_record.size(); ++i)
        {
            ss << " [" << m_record[i].first << "]: " << std::setiosflags(std::ios::fixed)
               << std::setprecision(4) << elapseds[i].count();
        }

        std::chrono::duration<double> totalElapsed = end - m_record[0].second;
        BCOS_LOG(DEBUG) << "[TIME RECORDER]-" << m_function
                        << ": [TOTAL]: " << std::setiosflags(std::ios::fixed)
                        << std::setprecision(4) << totalElapsed.count() << ss.str();

        m_name = "";
        m_timePoint = std::chrono::steady_clock::time_point();
        m_record.clear();
    }
}

std::string newSeq()
{
    static std::atomic<size_t> seq;
    size_t seqTmp = seq.fetch_add(1) + 1;
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(32) << seqTmp;
    return ss.str();
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
