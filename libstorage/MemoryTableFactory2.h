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
#include <tbb/spin_mutex.h>
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
const uint64_t ENTRY_ID_START = 100000;
class MemoryTableFactory2 : public TableFactory
{
public:
    using Ptr = std::shared_ptr<MemoryTableFactory2>;
    MemoryTableFactory2(bool enableReconfirmCommittee);
    MemoryTableFactory2(const MemoryTableFactory2&) = delete;
    MemoryTableFactory2(MemoryTableFactory2&&) = delete;
    MemoryTableFactory2& operator=(const MemoryTableFactory2&) = delete;
    MemoryTableFactory2& operator=(MemoryTableFactory2&&) = delete;
    ~MemoryTableFactory2() noexcept override = default;
    virtual void init();

    Table::Ptr openTable(
        const std::string& tableName, bool authorityFlag = true, bool isPara = true) override;
    Table::Ptr createTable(const std::string& tableName, const std::string& keyField,
        const std::string& valueField, bool authorityFlag = true,
        Address const& _origin = Address(), bool isPara = true) override;

    virtual uint64_t ID();
    h256 hash() override;
    size_t savepoint() override;
    void commit() override;
    void rollback(size_t _savepoint) override;
    void commitDB(h256 const& _blockHash, int64_t _blockNumber) override;

private:
    virtual Table::Ptr openTableWithoutLock(
        const std::string& tableName, bool authorityFlag = true, bool isPara = true);

    void setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo);
    std::vector<Change>& getChangeLog();
    uint64_t m_ID = 1;
    // this map can't be changed, hash() need ordered data
    tbb::concurrent_unordered_map<std::string, Table::Ptr> m_name2Table;
    tbb::enumerable_thread_specific<std::vector<Change> > s_changeLog;
    h256 m_hash;
    std::vector<std::string> m_sysTables;

    // mutex
    mutable tbb::spin_mutex x_name2Table;

    bool m_enableReconfirmCommittee;
};

}  // namespace storage

}  // namespace dev
