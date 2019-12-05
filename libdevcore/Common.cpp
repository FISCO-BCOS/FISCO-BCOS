/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file Common.h
 * @author Asherli
 * @date 2018
 *
 * common stuff: common type definitions && timer && ScopeGuard && InvariantChecker
 */
#include "Common.h"
#include "Exceptions.h"
#include <csignal>
#ifdef __APPLE__
#include <pthread.h>
#endif

using namespace std;

namespace dev
{
bytes const NullBytes;
std::string const EmptyString;

void InvariantChecker::checkInvariants(
    HasInvariants const* _this, char const* _fn, char const* _file, int _line, bool _pre)
{
    if (!_this->invariants())
    {
        LOG(WARNING) << (_pre ? "Pre" : "Post") << "invariant failed in" << _fn << "at" << _file
                     << ":" << _line;
        ::boost::exception_detail::throw_exception_(FailedInvariant(), _fn, _file, _line);
    }
}

TimerHelper::~TimerHelper()
{
    auto e = std::chrono::high_resolution_clock::now() - m_t;
    if (!m_ms || e > chrono::milliseconds(m_ms))
        LOG(DEBUG) << m_id << " " << chrono::duration_cast<chrono::milliseconds>(e).count()
                   << " ms";
}

/// get utc time(ms)
uint64_t utcTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/// get utc time(us)
uint64_t utcTimeUs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

std::string getCurrentDateTime()
{
    using std::chrono::system_clock;
    char buffer[40];
    auto currentTime = system_clock::to_time_t(system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    return std::string(buffer);
}

void errorExit(std::stringstream& _exitInfo, Exception const& exception)
{
    LOG(ERROR) << _exitInfo.str();
    std::cerr << _exitInfo.str();
    raise(SIGTERM);
    BOOST_THROW_EXCEPTION(exception << errinfo_comment(_exitInfo.str()));
}

thread_local std::string TimeRecorder::m_name;
thread_local std::chrono::system_clock::time_point TimeRecorder::m_timePoint;
thread_local size_t TimeRecorder::m_heapCount = 0;
thread_local std::vector<std::pair<std::string, std::chrono::system_clock::time_point> >
    TimeRecorder::m_record;

TimeRecorder::TimeRecorder(const std::string& function, const std::string& name)
  : m_function(function)
{
    auto now = std::chrono::system_clock::now();
    if (m_timePoint == std::chrono::system_clock::time_point())
    {
        m_name = name;
        m_timePoint = now;
    }
    else
    {
        // std::chrono::duration<double> elapsed = now - m_timePoint;
        m_record.push_back(std::make_pair(m_name, m_timePoint));

        m_name = name;
        m_timePoint = now;
    }

    ++m_heapCount;
}

TimeRecorder::~TimeRecorder()
{
    --m_heapCount;

    if (!m_heapCount && m_timePoint != std::chrono::system_clock::time_point())
    {
        auto now = std::chrono::system_clock::now();
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
        LOG(DEBUG) << "[TIME RECORDER]-" << m_function
                   << ": [TOTAL]: " << std::setiosflags(std::ios::fixed) << std::setprecision(4)
                   << totalElapsed.count() << ss.str();

        m_name = "";
        m_timePoint = std::chrono::system_clock::time_point();
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

}  // namespace dev

void dev::pthread_setThreadName(std::string const& _n)
{
#if defined(__GLIBC__)
    pthread_setname_np(pthread_self(), _n.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(_n.c_str());
#endif
}
