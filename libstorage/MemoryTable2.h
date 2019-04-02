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
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
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
    using CacheType = tbb::concurrent_unordered_map<uint32_t, Entry::Ptr>;
    using CacheIter = typename CacheType::iterator;
    using Ptr = std::shared_ptr<MemoryTable2>;

    MemoryTable2();

    virtual ~MemoryTable2(){};

    virtual Entries::Ptr select(const std::string& key, Condition::Ptr condition) override;

    virtual int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;

    virtual int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>(),
        bool needSelect = true) override;

    virtual int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override;

    virtual h256 hash() override;

    virtual void clear() override { m_dirty.clear(); }
    virtual bool empty() override
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

    void setStateStorage(Storage::Ptr amopDB) override { m_remoteDB = amopDB; }
    void setBlockHash(h256 blockHash) override { m_blockHash = blockHash; }
    void setBlockNum(int blockNum) override { m_blockNum = blockNum; }

    bool checkAuthority(Address const& _origin) const override
    {
        if (m_tableInfo->authorizedAddress.empty())
            return true;
        auto it = find(m_tableInfo->authorizedAddress.cbegin(),
            m_tableInfo->authorizedAddress.cend(), _origin);
        return it != m_tableInfo->authorizedAddress.cend();
    }

    virtual bool dump(dev::storage::TableData::Ptr data) override
    {
        data->info = m_tableInfo;
        data->entries = std::make_shared<Entries>();
        for (auto it : m_dirty)
        {
            data->entries->addEntry(it.second);
        }

        for (size_t i = 0; i < m_newEntries->size(); ++i)
        {
            data->entries->addEntry(m_newEntries->get(i));
        }

        return true;
    }

    virtual void rollback(const Change& _change) override;

private:
    using EntriesType = ConcurrentEntries;
    using EntriesPtr = EntriesType::Ptr;
    EntriesPtr m_newEntries;

    CacheType m_dirty;

    std::vector<size_t> processEntries(EntriesType::Ptr entries, Condition::Ptr condition)
    {
        std::vector<size_t> indexes;
        indexes.reserve(entries->size());
        if (condition->getConditions()->empty())
        {
            for (size_t i = 0; i < entries->size(); ++i)
            {
                /*
                When dereferencing a entry pointer(named as entry_ptr) contained in entries(name as
                v), we MUST need to check whether entry_ptr.get() equals to nullptr. It is NOT safe
                to use a subscript to visit elements in a tbb::concurrent_vector even though the
                subscript < v.size(), due to that the elements in the vector may not be already
                constructed.

                In the implementation of our ConcurrentEntries, we specify that the
                concurrent_vector container using tbb::zero_allocator<T, A> as its memory allocator.
                Zero allocator forwards allocation requests to A and zeros the allocation before
                returning it, which means the return value of get() method of an uncontructed
                entry_ptr would keep as 0x0(nullptr), until the entry constructed successfully. If
                the pointer is null, we just skip this element.
                */
                if (entries->get(i).get() != nullptr)
                {
                    indexes.emplace_back(i);
                }
            }
            return indexes;
        }

        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr entry = entries->get(i);
            // same as above
            if (entry != nullptr)
            {
                if (processCondition(entry, condition))
                {
                    indexes.push_back(i);
                }
            }
        }

        return indexes;
    }

    bool processCondition(Entry::Ptr entry, Condition::Ptr condition)
    {
        try
        {
            for (auto& it : *condition->getConditions())
            {
                if (entry->getStatus() == Entry::Status::DELETED)
                {
                    return false;
                }

                std::string lhs = entry->getField(it.first);
                std::string rhs = it.second.second;

                if (it.second.first == Condition::Op::eq)
                {
                    if (lhs != rhs)
                    {
                        return false;
                    }
                }
                else if (it.second.first == Condition::Op::ne)
                {
                    if (lhs == rhs)
                    {
                        return false;
                    }
                }
                else
                {
                    if (lhs.empty())
                    {
                        lhs = "0";
                    }
                    if (rhs.empty())
                    {
                        rhs = "0";
                    }

                    int lhsNum = boost::lexical_cast<int>(lhs);
                    int rhsNum = boost::lexical_cast<int>(rhs);

                    switch (it.second.first)
                    {
                    case Condition::Op::eq:
                    case Condition::Op::ne:
                    {
                        break;
                    }
                    case Condition::Op::gt:
                    {
                        if (lhsNum <= rhsNum)
                        {
                            return false;
                        }
                        break;
                    }
                    case Condition::Op::ge:
                    {
                        if (lhsNum < rhsNum)
                        {
                            return false;
                        }
                        break;
                    }
                    case Condition::Op::lt:
                    {
                        if (lhsNum >= rhsNum)
                        {
                            return false;
                        }
                        break;
                    }
                    case Condition::Op::le:
                    {
                        if (lhsNum > rhsNum)
                        {
                            return false;
                        }
                        break;
                    }
                    }
                }
            }
        }
        catch (std::exception& e)
        {
            STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable") << LOG_DESC("Compare error")
                               << LOG_KV("msg", boost::diagnostic_information(e));
            return false;
        }

        return true;
    }

    bool isHashField(const std::string& _key)
    {
        if (!_key.empty())
        {
            return ((_key.substr(0, 1) != "_" && _key.substr(_key.size() - 1, 1) != "_") ||
                    (_key == STATUS));
        }
        return false;
    }

    void checkField(Entry::Ptr entry)
    {
        for (auto& it : *(entry->fields()))
        {
            if (m_tableInfo->fields.end() ==
                find(m_tableInfo->fields.begin(), m_tableInfo->fields.end(), it.first))
            {
                STORAGE_LOG(ERROR)
                    << LOG_BADGE("MemoryTable") << LOG_DESC("field doen not exist")
                    << LOG_KV("table name", m_tableInfo->name) << LOG_KV("field", it.first);

                throw std::invalid_argument("Invalid key.");
            }
        }
    }

    Storage::Ptr m_remoteDB;
    TableInfo::Ptr m_tableInfo;
    h256 m_blockHash;
    int m_blockNum = 0;
};
}  // namespace storage
}  // namespace dev
