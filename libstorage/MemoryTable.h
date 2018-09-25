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
/** @file MemoryTable.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Storage.h"
#include "Table.h"

namespace dev
{
namespace storage
{
class MemoryTable : public Table
{
public:
    typedef std::shared_ptr<MemoryTable> Ptr;

    virtual ~MemoryTable(){};

    virtual void init(const std::string& tableName);
    virtual Entries::Ptr select(const std::string& key, Condition::Ptr condition) override;
    virtual size_t update(
        const std::string& key, Entry::Ptr entry, Condition::Ptr condition) override;
    virtual size_t insert(const std::string& key, Entry::Ptr entry) override;
    virtual size_t remove(const std::string& key, Condition::Ptr condition) override;

    virtual h256 hash();
    virtual void clear();
    virtual std::map<std::string, Entries::Ptr>* data() override;

    void setStateStorage(Storage::Ptr amopDB);

    void setBlockHash(h256 blockHash);
    void setBlockNum(int blockNum);

private:
    Entries::Ptr processEntries(Entries::Ptr entries, Condition::Ptr condition);
    bool processCondition(Entry::Ptr entry, Condition::Ptr condition);

    Storage::Ptr m_remoteDB;
    TableInfo::Ptr m_tableInfo;
    std::map<std::string, Entries::Ptr> m_cache;

    h256 m_blockHash;
    int m_blockNum = 0;
};

}  // namespace storage

}  // namespace dev
