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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file SealerPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Storage.h"
#include "StorageException.h"
#include "Table.h"
#include <json/json.h>
#include <leveldb/db.h>
#include <libutilities/BasicLevelDB.h>
#include <libutilities/FixedHash.h>

namespace bcos
{
namespace storage
{
class LevelDBStorage : public Storage
{
public:
    typedef std::shared_ptr<LevelDBStorage> Ptr;

    virtual ~LevelDBStorage(){};

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;
    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;

    void setDB(std::shared_ptr<bcos::db::BasicLevelDB> db);

private:
    size_t commitTableDataRange(std::shared_ptr<bcos::db::LevelDBWriteBatch>& batch,
        TableData::Ptr tableData, int64_t num, size_t from, size_t to);
    std::shared_ptr<bcos::db::BasicLevelDB> m_db;
    bcos::SharedMutex m_remoteDBMutex;
};

}  // namespace storage

}  // namespace bcos
