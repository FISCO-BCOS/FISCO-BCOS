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
#include "easylog.h"

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

thread_local std::string TimeRecorder::m_name;
thread_local std::chrono::system_clock::time_point TimeRecorder::m_timePoint;
thread_local std::vector<std::pair<std::string, std::chrono::duration<double> > > TimeRecorder::m_record;

TimeRecorder::TimeRecorder(const std::string &function, const std::string &name) {
	m_function = function;
	auto now = std::chrono::system_clock::now();
	if(m_timePoint == std::chrono::system_clock::time_point()) {
		m_name = name;
		m_timePoint == now;
	}
	else {
		std::chrono::duration<double> elapsed = now - m_timePoint;
		m_record.push_back(std::make_pair(name, elapsed));

		m_name = name;
		m_timePoint = now;
	}
}

TimeRecorder::~TimeRecorder() {
	if(m_timePoint !=  std::chrono::system_clock::time_point()) {
		auto now = std::chrono::system_clock::now();
		std::chrono::duration<double> elapsed = now - m_timePoint;

		m_record.push_back(std::make_pair(m_name, elapsed));

		std::stringstream ss;
		for(auto it: m_record) {
			ss << " [" << it.first << "]: " << it.second.count();
		}

		LOG(DEBUG) << "TIME RECORDER- " << m_function << ":" << ss.str();

		m_name = "";
		m_timePoint = std::chrono::time_point<std::chrono::system_clock,std::chrono::nanoseconds>();
		m_record.clear();
	}
}

}  // namespace dev
