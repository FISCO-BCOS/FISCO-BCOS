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

#include "MemoryTable.h"
#include "Storage.h"
#include "Table.h"
#include "libdevcrypto/CryptoInterface.h"
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libprecompiled/Common.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <mutex>
#include <thread>
#include <type_traits>

namespace dev
{
namespace storage
{
using Parallel = std::true_type;
using Serial = std::false_type;

template <typename Mode = Serial>
class MemoryTable : public Table
{
public:
    using ConcurrentMap = tbb::concurrent_unordered_map<std::string, Entries::Ptr>;
    using SerialMap = std::map<std::string, Entries::Ptr>;

    using CacheType = typename std::conditional<Mode::value, ConcurrentMap, SerialMap>::type;
    using CacheIter = typename CacheType::iterator;
    using KeyType = typename CacheType::key_type;

    using Ptr = std::shared_ptr<MemoryTable<Mode>>;

    virtual ~MemoryTable(){};

    typename Entries::ConstPtr select(const std::string& key, Condition::Ptr condition) override
    {
        try
        {
            auto entries = selectFromCache(key);
            auto indexes = processEntries(entries, condition);
            typename Entries::Ptr resultEntries = std::make_shared<Entries>();
            for (auto& i : indexes)
            {
                resultEntries->addEntry(entries->get(i));
            }
            return resultEntries;
        }
        catch (std::exception& e)
        {
            STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable") << LOG_DESC("Table select failed for")
                               << LOG_KV("msg", boost::diagnostic_information(e));
        }

        return std::make_shared<Entries>();
    }

    virtual int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override
    {
        try
        {
            if (options->check && !checkAuthority(options->origin))
            {
                return storage::CODE_NO_AUTHORIZED;
            }

            auto entries = selectFromCache(key);

            if (!entries || entries->size() == 0)
            {
                return 0;
            }

            checkField(entry);
            auto indexes = processEntries(entries, condition);
            std::vector<Change::Record> records;

            for (auto& i : indexes)
            {
                Entry::Ptr updateEntry = entries->get(i);
                for (auto& it : *(entry))
                {
                    records.emplace_back(i, it.first, updateEntry->getField(it.first));
                    updateEntry->setField(it.first, it.second);
                }
            }
            if (m_recorder)
            {
                this->m_recorder(this->shared_from_this(), Change::Update, key, records);
            }

            entries->setDirty(true);

            return indexes.size();
        }
        catch (std::exception& e)
        {
            STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable")
                               << LOG_DESC("Access MemoryTable failed for")
                               << LOG_KV("msg", boost::diagnostic_information(e));
        }

        return 0;
    }

    virtual int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>(),
        bool needSelect = true) override
    {
        try
        {
            if (options->check && !checkAuthority(options->origin))
            {
                return storage::CODE_NO_AUTHORIZED;
            }

            Condition::Ptr condition = std::make_shared<Condition>();

            auto entries = selectFromCache(key, needSelect);

            checkField(entry);

            // Bug: Not set key, but for compatible with RC1, leave bug here. This MemoryTable will
            // be instead by MemoryTable2, not to fix any more.
            // entry->setField(m_tableInfo->key, key); bug fix here

            Change::Record record(entries->size());
            std::vector<Change::Record> value{record};
            if (m_recorder)
            {
                this->m_recorder(this->shared_from_this(), Change::Insert, key, value);
            }
            if (entries->size() == 0)
            {
                entries->addEntry(entry);
                m_cache.insert(std::make_pair(key, entries));
                return 1;
            }
            else
            {
                entries->addEntry(entry);
                return 1;
            }
        }
        catch (std::exception& e)
        {
            STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable")
                               << LOG_DESC("Access MemoryTable failed for")
                               << LOG_KV("msg", boost::diagnostic_information(e));
        }

        return 1;
    }

    virtual int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) override
    {
        if (options->check && !checkAuthority(options->origin))
        {
            return storage::CODE_NO_AUTHORIZED;
        }

        auto entries = selectFromCache(key);

        auto indexes = processEntries(entries, condition);

        std::vector<Change::Record> records;
        for (auto& i : indexes)
        {
            Entry::Ptr removeEntry = entries->get(i);

            removeEntry->setStatus(1);
            records.emplace_back(i);
        }
        if (m_recorder)
        {
            this->m_recorder(this->shared_from_this(), Change::Remove, key, records);
        }

        entries->setDirty(true);

        return indexes.size();
    }

    virtual h256 hash() override
    {
        std::map<std::string, Entries::Ptr> tmpOrderedCache(m_cache.begin(), m_cache.end());
        bytes data;
        for (auto& it : tmpOrderedCache)
        {
            if (it.second != nullptr && it.second->dirty())
            {
                data.insert(data.end(), it.first.begin(), it.first.end());
                for (size_t i = 0; i < it.second->size(); ++i)
                {
                    if (it.second->get(i)->dirty() && !it.second->get(i)->deleted())
                    {
                        auto entry = it.second->get(i);
                        entry->setField(
                            STATUS, boost::lexical_cast<std::string>(entry->getStatus()));

                        for (auto& fieldIt : *(it.second->get(i)))
                        {
                            if (isHashField(fieldIt.first) || fieldIt.first == STATUS)
                            {
                                data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());
                                data.insert(
                                    data.end(), fieldIt.second.begin(), fieldIt.second.end());
                            }
                        }
                    }
                }
            }
        }

        if (data.empty())
        {
            return h256();
        }
        bytesConstRef bR(data.data(), data.size());
#ifdef FISCO_GM
        auto hash = dev::sm3(bR);
#else
        auto hash = dev::sha256(bR);
#endif
        return hash;
    }
    virtual void clear() override { m_cache.clear(); }
    virtual bool empty() override
    {
        for (auto iter : m_cache)
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

    virtual void setRecorder(std::function<void(
            Table::Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
            _recorder) override
    {
        m_recorder = _recorder;
    }

    virtual TableData::Ptr dump() override
    {
        auto _data = std::make_shared<TableData>();
        bool dirtyTable = false;

        for (auto it : m_cache)
        {
            _data->data.insert(make_pair(it.first, it.second));

            if (it.second->dirty())
            {
                dirtyTable = true;
            }
        }

        if (dirtyTable)
        {
            return _data;
        }
        else
        {
            return std::make_shared<TableData>();
        }
    }

    virtual void rollback(const Change& _change) override
    {
        {
            switch (_change.kind)
            {
            case Change::Insert:
            {
                auto entries = m_cache[_change.key];
                entries->removeEntry(_change.value[0].index);
                if (entries->size() == 0u)
                {
                    eraseKey(m_cache, _change.key);
                }
                break;
            }
            case Change::Update:
            {
                auto entries = m_cache[_change.key];
                for (auto& record : _change.value)
                {
                    auto entry = entries->get(record.index);
                    entry->setField(record.key, record.oldValue);
                }
                break;
            }
            case Change::Remove:
            {
                auto entries = m_cache[_change.key];
                for (auto& record : _change.value)
                {
                    auto entry = entries->get(record.index);
                    entry->setStatus(0);
                }
                break;
            }
            case Change::Select:

            default:
                break;
            }
        }
    }

    size_t cacheSize() override { return m_cache.size(); }

private:
    /*
    NOTICE:
    m_eraseMutex is used for eraseKey() method with tbb::concurrent_unordered_map
    instance only. Because in that eraseKey() method, we use unsafe_erase() method to delete element
    in tbb::concurrent_unordered_map instance. As "unsafe_" prefix says, the unsafe_erase() method
    can NOT ensure concurrenty safety between different calls to this method, that's why we used a
    mutex to ensure there is only one thread can erase element each time.

    Additionally, by now no evidence shows that unsafe_erase() method would conflict with insert()
    or find() method. To get furthur details about the data structure used in
    tbb::concurrent_unordered_map, maybe you could read:
    http://www.cs.ucf.edu/~dcm/Teaching/COT4810-Spring2011/Literature/SplitOrderedLists.pdf
    */
    std::mutex m_eraseMutex;

    void eraseKey(ConcurrentMap& _map, KeyType const& _key)
    {
        std::lock_guard<std::mutex> lock(m_eraseMutex);
        _map.unsafe_erase(_key);
    }

    void eraseKey(SerialMap& _map, KeyType const& _key) { _map.erase(_key); }

    typename Entries::Ptr selectFromCache(const std::string& key, bool needSelect = true)
    {
        auto entries = std::make_shared<Entries>();

        CacheIter it = m_cache.find(key);
        if (it == m_cache.end())
        {
            // These code is fast with no rollback
            if (m_remoteDB && needSelect)
            {
                entries = m_remoteDB->select(m_blockNum, m_tableInfo, key);
                // Multiple insertion is ok in concurrent_unordered_map, the second insert will be
                // dropped.
                m_cache.insert(std::make_pair(key, entries));
            }
        }
        else
        {
            entries = it->second;
        }

        return entries;
    }

    std::vector<size_t> processEntries(Entries::Ptr entries, Condition::Ptr condition)
    {
        std::vector<size_t> indexes;
        indexes.reserve(entries->size());
        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr entry = entries->get(i);
            if (condition->process(entry))
            {
                indexes.push_back(i);
            }
        }
        if (condition->getOffset() >= 0 && condition->getCount() >= 0)
        {
            int begin = condition->getOffset();
            int end = 0;

            if (g_BCOSConfig.version() < V2_1_0)
            {
                end = begin + condition->getCount();
            }
            else
            {
                end = (int)std::min((size_t)INT_MAX, (size_t)begin + (size_t)condition->getCount());
            }

            std::vector<size_t> limitedIndex;
            int size = indexes.size();
            if (begin >= size)
            {
                return limitedIndex;
            }
            return std::vector<size_t>(indexes.begin() + begin,
                indexes.begin() + end > indexes.end() ? indexes.end() : indexes.begin() + end);
        }

        return indexes;
    }


    void checkField(Entry::Ptr entry)
    {
        for (auto& it : *(entry))
        {
            if (it.first == ID_FIELD)
            {
                continue;
            }
            if (m_tableInfo->fields.end() ==
                find(m_tableInfo->fields.begin(), m_tableInfo->fields.end(), it.first))
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid key."));
            }
        }
    }

    CacheType m_cache;
    std::function<void(Table::Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
        m_recorder;
};
}  // namespace storage
}  // namespace dev
