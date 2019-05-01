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
/** @file MemoryTable2.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTable2.h"
#include "Common.h"
#include "Table.h"
#include <arpa/inet.h>
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libprecompiled/Common.h>
#include <tbb/parallel_sort.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <thread>
#include <vector>

using namespace dev;
using namespace dev::storage;
using namespace dev::precompiled;

MemoryTable2::MemoryTable2() : m_newEntries(std::make_shared<Entries>()) {}

Entries::ConstPtr MemoryTable2::select(const std::string& key, Condition::Ptr condition)
{
    return selectNoLock(key, condition);
}

Entries::Ptr MemoryTable2::selectNoLock(const std::string& key, Condition::Ptr condition)
{
    try
    {
        auto entries = std::make_shared<Entries>();
        condition->EQ(m_tableInfo->key, key);
        if (m_remoteDB)
        {
            // query remoteDB anyway
            Entries::Ptr dbEntries =
                m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo, key, condition);

            if (!dbEntries)
            {
                return entries;
            }

            for (size_t i = 0; i < dbEntries->size(); ++i)
            {
                auto entryIt = m_dirty.find(dbEntries->get(i)->getID());
                if (entryIt != m_dirty.end())
                {
                    entries->addEntry(entryIt->second);
                }
                else
                {
                    entries->addEntry(dbEntries->get(i));
                }
            }
        }

        auto indices = processEntries(m_newEntries, condition);
        for (auto it : indices)
        {
            m_newEntries->get(it)->setTempIndex(it);
            entries->addEntry(m_newEntries->get(it));
        }

        return entries;
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2") << LOG_DESC("Table select failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return std::make_shared<Entries>();
}

int MemoryTable2::update(
    const std::string& key, Entry::Ptr entry, Condition::Ptr condition, AccessOptions::Ptr options)
{
    try
    {
        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("update permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);

            return storage::CODE_NO_AUTHORIZED;
        }

        checkField(entry);

        auto entries = selectNoLock(key, condition);
        std::vector<Change::Record> records;

        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr updateEntry = entries->get(i);

            // if id not equals to zero and not in the m_dirty, must be new dirty entry
            if (updateEntry->getID() != 0 && m_dirty.find(updateEntry->getID()) == m_dirty.end())
            {
                m_dirty.insert(std::make_pair(updateEntry->getID(), updateEntry));
            }

            for (auto& it : *(entry->fields()))
            {
                //_id_ always got initialized value 0 from Entry::Entry()
                // no need to update _id_ while updating entry
                if (it.first != "_id_" && it.first != m_tableInfo->key)
                {
                    records.emplace_back(updateEntry->getTempIndex(), it.first,
                        updateEntry->getField(it.first), updateEntry->getID());
                    updateEntry->setField(it.first, it.second);
                }
            }
        }

        m_recorder(shared_from_this(), Change::Update, key, records);

        return entries->size();
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

int MemoryTable2::insert(
    const std::string& key, Entry::Ptr entry, AccessOptions::Ptr options, bool needSelect)
{
    try
    {
        (void)needSelect;

        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("insert permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
            return storage::CODE_NO_AUTHORIZED;
        }

        checkField(entry);

        entry->setField(m_tableInfo->key, key);
        // Change::Record record(m_newEntries->size());
        auto iter = m_newEntries->addEntry(entry);
        Change::Record record(iter);

        std::vector<Change::Record> value{record};
        m_recorder(shared_from_this(), Change::Insert, key, value);

        return 1;
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

int MemoryTable2::remove(
    const std::string& key, Condition::Ptr condition, AccessOptions::Ptr options)
{
    try
    {
        if (options->check && !checkAuthority(options->origin))
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable2")
                                 << LOG_DESC("remove permission denied")
                                 << LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
            return storage::CODE_NO_AUTHORIZED;
        }

        auto entries = selectNoLock(key, condition);

        std::vector<Change::Record> records;
        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr removeEntry = entries->get(i);

            removeEntry->setStatus(1);

            // if id not equals to zero and not in the m_dirty, must be new dirty entry
            if (removeEntry->getID() != 0 && m_dirty.find(removeEntry->getID()) == m_dirty.end())
            {
                m_dirty.insert(std::make_pair(removeEntry->getID(), removeEntry));
            }

            records.emplace_back(removeEntry->getTempIndex(), "", "", removeEntry->getID());
        }

        m_recorder(shared_from_this(), Change::Remove, key, records);

        return entries->size();
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable2")
                           << LOG_DESC("Access MemoryTable2 failed for")
                           << LOG_KV("msg", boost::diagnostic_information(e));
    }

    return 0;
}

dev::h256 MemoryTable2::hash()
{
    auto tempEntries = std::vector<Entry::Ptr>();
    auto size = m_dirty.size() + m_newEntries->size();
    tempEntries.resize(size);

    tbb::atomic<size_t> j = 0;
    tbb::parallel_for(m_dirty.range(),
        [&](tbb::concurrent_unordered_map<uint32_t, Entry::Ptr>::range_type& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                tempEntries[j.fetch_and_increment()] = (*it).second;
            }
        });

    // parallel import unless > 10 * 1000 items
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_newEntries->size(), 10 * 1000),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                tempEntries[j.fetch_and_increment()] = m_newEntries->get(i);
            }
        });

    tbb::parallel_sort(tempEntries.begin(), tempEntries.end(), EntryLess(m_tableInfo));

    bytes data;
    for (size_t i = 0; i < tempEntries.size(); ++i)
    {
        auto entry = tempEntries[i];
        if (!entry->deleted())
        {
            for (auto fieldIt : *(entry->fields()))
            {
                if (isHashField(fieldIt.first))
                {
                    data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());
                    data.insert(data.end(), fieldIt.second.begin(), fieldIt.second.end());
                }
            }
        }
    }

    if (data.empty())
    {
        return h256();
    }

    bytesConstRef bR(data.data(), data.size());
    h256 hash = dev::sha256(bR);

    return hash;
}

bool MemoryTable2::dump(dev::storage::TableData::Ptr data)
{
    data->info = m_tableInfo;
    data->dirtyEntries = std::make_shared<Entries>();

    tbb::parallel_for(m_dirty.range(),
        [&](tbb::concurrent_unordered_map<uint32_t, Entry::Ptr>::range_type& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                if (!it->second->deleted())
                {
                    data->dirtyEntries->addEntry(it->second);
                }
            }
        });

    data->newEntries = std::make_shared<Entries>();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_newEntries->size(), 1000),
        [&](const tbb::blocked_range<size_t>& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                if (!m_newEntries->get(i)->deleted())
                {
                    data->newEntries->addEntry(m_newEntries->get(i));
                }
            }
        });

    return true;
}

void MemoryTable2::rollback(const Change& _change)
{
    LOG(TRACE) << "Before rollback newEntries size: " << m_newEntries->size();
    for (size_t i = 0; i < m_newEntries->size(); ++i)
    {
        auto entry = m_newEntries->get(i);

        std::stringstream ss;
        ss << i;
        ss << "," << entry->getStatus();

        for (auto it : *(entry->fields()))
        {
            ss << "," << it.first << ":" << it.second;
        }
        LOG(TRACE) << ss.str();
    }

    switch (_change.kind)
    {
    case Change::Insert:
    {
        LOG(TRACE) << "Rollback insert record newIndex: " << _change.value[0].index;

        auto entry = m_newEntries->get(_change.value[0].index);
        // entry->setStatus(1);
        entry->setDeleted(true);
        // m_newEntries->removeEntry(_change.value[0].newIndex);
        break;
    }
    case Change::Update:
    {
        for (auto& record : _change.value)
        {
            LOG(TRACE) << "Rollback update record id: " << record.id
                       << " newIndex: " << record.index;
            if (record.id)
            {
                auto it = m_dirty.find(record.id);
                if (it != m_dirty.end())
                {
                    it->second->setField(record.key, record.oldValue);
                }
            }
            else
            {
                auto entry = m_newEntries->get(record.index);
                entry->setField(record.key, record.oldValue);
            }
        }
        break;
    }
    case Change::Remove:
    {
        for (auto& record : _change.value)
        {
            LOG(TRACE) << "Rollback remove record id: " << record.id
                       << " newIndex: " << record.index;
            if (record.id)
            {
                auto it = m_dirty.find(record.id);
                if (it != m_dirty.end())
                {
                    it->second->setStatus(0);
                }
            }
            else
            {
                auto entry = m_newEntries->get(record.index);
                entry->setStatus(0);
            }
        }
        break;
    }
    case Change::Select:

    default:
        break;
    }

    LOG(TRACE) << "After rollback newEntries size: " << m_newEntries->size();
}
