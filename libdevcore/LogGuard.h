/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: LogGuard.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include "StateMonitor.h"
#include "easylog.h"
#include "StatCommon.h"

namespace dev
{

class StatisticsLog{};

class LogGuard : public LogOutputStreamBase
{
protected:
    LogGuard(const std::string _tag) : LogOutputStreamBase(), m_tag{_tag} { }

    virtual ~LogGuard() {}

protected:
    std::string m_tag;
};

class TimeIntervalLogGuard : public LogGuard
{
public:
    TimeIntervalLogGuard(const int _code, const std::string _tag, uint64_t _interval) 
        : LogGuard{_tag}, m_code{_code}
    {
        if (_interval <= 0) 
        {
            _interval = 10; // 10s
        }
        statemonitor::recordStateByTimeStart(m_code, _interval);
    }
    virtual ~TimeIntervalLogGuard() 
    {
        statemonitor::recordStateByTimeEnd(m_code, move(m_tag), m_sstr.str(), m_success);
    }   
protected:
    int m_success = 1;
private:
    int m_code;
};


class CountIntervalLogGuard : public LogGuard 
{
public:
    CountIntervalLogGuard(const int _code, const std::string _tag, uint64_t _count) 
        : LogGuard{_tag}, m_code{_code}
    {
        if (_count <= 0) 
        {
            _count = 10; // 10s
        }
        statemonitor::recordStateByNumStart(m_code, _count);
    }
    virtual ~CountIntervalLogGuard() 
    {
        statemonitor::recordStateByNumEnd(m_code, move(m_tag), m_sstr.str());
    }
protected:
    int m_success = 1;
private:
    int m_code;
};

class NormalLogGuard : public CountIntervalLogGuard 
{
public:
    NormalLogGuard(const int _code, const std::string _tag) : CountIntervalLogGuard(_code, _tag, 1) {}
};

class NoCodeLogGuard : public CountIntervalLogGuard
{
public:
    NoCodeLogGuard(const std::string _tag) : CountIntervalLogGuard(StatCode::NO_CODE + irrelevant_inc % NO_CODE_GAP, _tag, 1) 
    {
        irrelevant_inc++;
    }
private:
    static int irrelevant_inc;
    static const int NO_CODE_GAP = 5000;
};


class ErrorLogGuard : public NormalLogGuard 
{
public:
    ErrorLogGuard(const std::string _tag) : NormalLogGuard(StatCode::ERROR_CODE, _tag) {}
};


#define STAT_LINE_NAME(_obj_name, _line) _obj_name##_line
#define STAT_OBJ(_obj_name, _line) STAT_LINE_NAME(_obj_name, _line)

//  __LINE__ 
#define STAT_INNER_FUN_LOGGUARD(_obj, _code, _tag) NormalLogGuard _obj(_code + __LINE__, _tag);

#define STAT_MSG_LOGGUARD(_code, _tag, _msg) { NormalLogGuard STAT_OBJ(STAT_OBJ, __LINE__)(_code, _tag); STAT_OBJ(STAT_OBJ, __LINE__) << _msg; }

#define STAT_ERROR_MSG_LOGGUARD_MSG(_tag, _msg) { NormalLogGuard STAT_OBJ(STAT_OBJ, __LINE__)(StatCode::ERROR_CODE, _tag); STAT_OBJ(STAT_OBJ, __LINE__) << _msg; }

#define STAT_ERROR_MSG_LOGGUARD(_tag) NormalLogGuard STAT_OBJ(STAT_OBJ, __LINE__)(StatCode::ERROR_CODE, _tag); STAT_OBJ(STAT_OBJ, __LINE__) 

#define STAT_MSG_NOCODE_LOGGUARD(_tag, _msg) { NoCodeLogGuard STAT_OBJ(STAT_OBJ, __LINE__)(_tag); STAT_OBJ(STAT_OBJ, __LINE__) << _msg; }


}