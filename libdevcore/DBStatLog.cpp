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
 * @file: DBStatLog.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "DBStatLog.h"

using namespace statemonitor;
namespace dev
{

// 先设置为1min上报一次，以后可能通过配置文件或者其他的方式更改
uint64_t DBGetLogGuard::report_interval(60);  // 1min

uint64_t DBSetLogGuard::report_interval(60);

uint64_t DBMemHitGuard::report_interval(60);

uint64_t DBInterval::DB_get_size_interval(60);

uint64_t DBInterval::DB_set_size_interval(60);

void statGetDBSizeLog(uint64_t s)
{
    recordStateByTimeOnce(StatCode::DB_GET_SIZE, DBInterval::DB_get_size_interval, (double)s, STAT_DB_GET_SIZE, "");
}

void statSetDBSizeLog(uint64_t s)
{
    recordStateByTimeOnce(StatCode::DB_SET_SIZE, DBInterval::DB_set_size_interval, (double)s, STAT_DB_SET_SIZE, "");
}

}