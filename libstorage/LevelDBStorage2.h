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
#include <json/json.h>
#include <leveldb/db.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <map>

namespace dev
{
namespace storage
{
class LevelDBStorage2 : public Storage
{
public:
    typedef std::shared_ptr<LevelDBStorage2> Ptr;

    virtual ~LevelDBStorage2(){};

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override;
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

    void setDB(std::shared_ptr<dev::db::BasicLevelDB> db);

private:
    void processNewEntries(h256 hash, int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries);

    void processDirtyEntries(h256 hash, int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries);

    std::shared_ptr<dev::db::BasicLevelDB> m_db;
};

}  // namespace storage

}  // namespace dev
