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
/** @file RocksDBStorage.h
 *  @author xingqiangbai
 *  @date 20180423
 */
#pragma once

#include "Storage.h"
#include <json/json.h>
#include <libutilities/FixedHash.h>
#include <tbb/spin_mutex.h>
#include <map>

namespace rocksdb
{
class DB;
}
namespace bcos
{
namespace storage
{
class BasicRocksDB;

class RocksDBStorage : public Storage
{
public:
    typedef std::shared_ptr<RocksDBStorage> Ptr;
    RocksDBStorage(bool _disableWAL = false, bool _shouldCompleteDirty = false)
      : m_disableWAL(_disableWAL), m_shouldCompleteDirty(_shouldCompleteDirty)
    {}
    virtual ~RocksDBStorage(){};

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override;
    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;

    void setDB(std::shared_ptr<BasicRocksDB> db) { m_db = db; }

private:
    bool m_disableWAL = false;
    bool m_shouldCompleteDirty = false;
    void processEntries(int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries, bool isDirtyEntries);

    void processDirtyEntries(int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries);

    std::shared_ptr<BasicRocksDB> m_db;
    tbb::spin_mutex m_writeBatchMutex;
};

}  // namespace storage

}  // namespace bcos
