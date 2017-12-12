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
 * @file: DBStatLog.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include "LogGuard.h"

namespace dev 
{

class DBGetLogGuard : public TimeIntervalLogGuard
{
public:
    DBGetLogGuard() : TimeIntervalLogGuard(StatCode::DB_GET, STAT_DB_GET, report_interval) {}
    static uint64_t report_interval;
};

class DBSetLogGuard : public TimeIntervalLogGuard
{
public:
    DBSetLogGuard() : TimeIntervalLogGuard(StatCode::DB_SET, STAT_DB_SET, report_interval) {}
    static uint64_t report_interval;
};

class DBMemHitGuard : public TimeIntervalLogGuard
{
public:
    DBMemHitGuard() : TimeIntervalLogGuard(StatCode::DB_HIT_MEM, STAT_DB_HIT_MEM, report_interval) 
    {
        m_success = 0;
    }
    void hit() { m_success = 1; }
    static uint64_t report_interval;
};

class DBInterval 
{
public:
    static uint64_t DB_get_size_interval;
    static uint64_t DB_set_size_interval;
};

void statGetDBSizeLog(uint64_t s);
void statSetDBSizeLog(uint64_t s);

}