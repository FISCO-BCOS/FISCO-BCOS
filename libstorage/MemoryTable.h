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
/** @file MemoryTable.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Storage.h"
#include "Table.h"
#include <libdevcore/Guards.h>
#include <tbb/concurrent_unordered_map.h>
#include <type_traits>

namespace dev
{
namespace storage
{
template <bool IsPara = false>
class MemoryTable : public Table<IsPara>
{
public:
    typedef std::shared_ptr<MemoryTable<IsPara>> Ptr;

    virtual ~MemoryTable(){};

    virtual void init(const std::string& tableName);
    virtual typename Entries<IsPara>::Ptr select(
        const std::string& key, Condition::Ptr condition) override;
    virtual int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;
    virtual int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;
    virtual int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;

    virtual h256 hash() override;
    virtual void clear() override;
    virtual typename Table<IsPara>::DataType* data() override;

    void setStateStorage(Storage::Ptr amopDB);
    void setBlockHash(h256 blockHash);
    void setBlockNum(int blockNum);
    void setTableInfo(TableInfo::Ptr tableInfo);

    bool checkAuthority(Address const& _origin) const override;

private:
    using CacheType = typename std::conditional<IsPara,
        tbb::concurrent_unordered_map<std::string, typename Entries<true>::Ptr>,
        std::unordered_map<std::string, typename Entries<false>::Ptr>>::type;
    using CacheItr = typename CacheType::iterator;

    std::vector<size_t> processEntries(
        typename Entries<IsPara>::Ptr entries, Condition::Ptr condition);
    bool processCondition(Entry::Ptr entry, Condition::Ptr condition);
    bool isHashField(const std::string& _key);
    void checkField(Entry::Ptr entry);
    Storage::Ptr m_remoteDB;
    TableInfo::Ptr m_tableInfo;
    CacheType m_cache;
    h256 m_blockHash;
    int m_blockNum = 0;
};

}  // namespace storage

}  // namespace dev
