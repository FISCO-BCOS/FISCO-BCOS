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
/** @file MemoryTableFactory.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Common.h"
#include "MemoryTable.h"
#include "Storage.h"
#include "Table.h"

#include <tbb/enumerable_thread_specific.h>
#include <boost/algorithm/string.hpp>
#include <boost/thread/tss.hpp>
#include <memory>
#include <type_traits>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace storage
{
class MemoryTableFactory2 : public TableFactory
{
public:
    typedef std::shared_ptr<MemoryTableFactory2> Ptr;
    MemoryTableFactory2();
    virtual ~MemoryTableFactory2() {}
    virtual void init();

    virtual Table::Ptr openTable(
        const std::string& tableName, bool authorityFlag = true, bool isPara = true) override;
    virtual Table::Ptr createTable(const std::string& tableName, const std::string& keyField,
        const std::string& valueField, bool authorityFlag = true,
        Address const& _origin = Address(), bool isPara = true) override;

    virtual uint64_t ID() { return m_ID; };
    virtual h256 hash() override;
    virtual size_t savepoint() override;
    virtual void commit() override;
    virtual void rollback(size_t _savepoint) override;
    virtual void commitDB(h256 const& _blockHash, int64_t _blockNumber) override;

private:
    void setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo);
    std::vector<Change>& getChangeLog();
    uint64_t m_ID = 1;
    // this map can't be changed, hash() need ordered data
    tbb::concurrent_unordered_map<std::string, Table::Ptr> m_name2Table;
    tbb::enumerable_thread_specific<std::vector<Change> > s_changeLog;
    h256 m_hash;
    std::vector<std::string> m_sysTables;

    // mutex
    mutable RecursiveMutex x_name2Table;
};

}  // namespace storage

}  // namespace dev
