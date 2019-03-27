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

}  // namespace dev
