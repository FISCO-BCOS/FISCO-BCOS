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
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcrypto/Hash.h>
#include <libprecompiled/Common.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <type_traits>

namespace dev
{
namespace storage
{
class MemoryTable2 : public Table
{
public:
    using Ptr = std::shared_ptr<MemoryTable2>;

    virtual ~MemoryTable2(){};

    Entries::ConstPtr select(const std::string& key, Condition::Ptr condition) override;

    int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;

    int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>(),
        bool needSelect = true) override;

    int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;

    h256 hash() override;

    void clear() override { m_dirty.clear(); }
    bool empty() override
    {
        for (auto iter : m_dirty)
        {
            if (iter.second != nullptr)
            {
                return false;
            }
        }

        return true;
    }

    bool checkAuthority(Address const& _origin) const override
    {
        if (m_tableInfo->authorizedAddress.empty())
            return true;
        auto it = find(m_tableInfo->authorizedAddress.cbegin(),
            m_tableInfo->authorizedAddress.cend(), _origin);
        return it != m_tableInfo->authorizedAddress.cend();
    }

    dev::storage::TableData::Ptr dump() override;

    void rollback(const Change& _change) override;
    void setEnableOptimize(bool const& _enableOptimize) { m_enableOptimize = _enableOptimize; }

private:
    Entries::Ptr selectNoLock(const std::string& key, Condition::Ptr condition);
    dev::storage::TableData::Ptr dumpWithoutOptimize();

    tbb::concurrent_unordered_map<std::string, Entries::Ptr> m_newEntries;
    tbb::concurrent_unordered_map<uint64_t, Entry::Ptr> m_dirty;

    std::vector<size_t> processEntries(Entries::Ptr entries, Condition::Ptr condition)
    {
        std::vector<size_t> indexes;
        indexes.reserve(entries->size());
        if (condition->empty())
        {
            for (size_t i = 0; i < entries->size(); ++i)
            {
                indexes.emplace_back(i);
            }
            return indexes;
        }

        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr entry = entries->get(i);
            // same as above
            if (entry != nullptr)
            {
                if (condition->process(entry))
                {
                    indexes.push_back(i);
                }
            }
        }

        return indexes;
    }

    void checkField(Entry::Ptr entry)
    {
        auto lock = entry->lock(false);

        for (auto& it : *(entry))
        {
            if (m_tableInfo->fields.end() ==
                find(m_tableInfo->fields.begin(), m_tableInfo->fields.end(), it.first))
            {
                STORAGE_LOG(ERROR)
                    << LOG_BADGE("MemoryTable") << LOG_DESC("field not exist")
                    << LOG_KV("table name", m_tableInfo->name) << LOG_KV("field", it.first);

                BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid key."));
            }
        }
    }

    void proccessLimit(const Condition::Ptr& condition, const Entries::Ptr& entries,
        const Entries::Ptr& resultEntries);

    bool m_isDirty = false;  // mark if the tableData had been dump
    dev::h256 m_hash;
    dev::storage::TableData::Ptr m_tableData;
    bool m_enableOptimize = false;
};
}  // namespace storage
}  // namespace dev
