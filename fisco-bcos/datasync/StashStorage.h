/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file StashStorage.h
 *  @author wesleywang
 *  @date 2021-5-10
 */
#pragma once

#include "StashSQLBasicAccess.h"
#include <json/json.h>
#include <libstorage/ZdbStorage.h>

#define StashStorage_LOG(LEVEL) LOG(LEVEL) << "[StashStorage] "

namespace dev
{
namespace storage
{
class StashStorage : public ZdbStorage
{
public:
    typedef std::shared_ptr<StashStorage> Ptr;

    StashStorage();
    virtual ~StashStorage(){};

    TableData::Ptr selectTableDataByNum(
        int64_t num, const TableInfo::Ptr& tableInfo, uint64_t start, uint32_t counts);


public:
    void setStashConnPool(std::shared_ptr<SQLConnectionPool>& _connPool);
    void SetStashSqlAccess(StashSQLBasicAccess::Ptr _sqlBasicAcc);

private:
    std::function<void(std::exception&)> m_fatalHandler;
    StashSQLBasicAccess::Ptr m_sqlBasicAcc;


private:
    int m_maxRetry = 60;
};

}  // namespace storage

}  // namespace dev
