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
/** @file MemoryTable.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTable.h"

#include "Table.h"
#include <json/json.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::storage;

void dev::storage::MemoryTable::init(const std::string& tableName)
{
    LOG(DEBUG) << "Init MemoryTable:" << tableName;

    m_tableInfo = m_remoteDB->info(tableName);
}

Entries::Ptr dev::storage::MemoryTable::select(const std::string& key, Condition::Ptr condition)
{
    try
    {
        LOG(DEBUG) << "Select MemoryTable: " << key;

        Entries::Ptr entries = std::make_shared<Entries>();

        auto it = m_cache.find(key);
        if (it == m_cache.end())
        {
            if (m_remoteDB.get() != NULL)
            {
                entries = m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo->name, key);

                LOG(DEBUG) << "AMOPDB selects:" << entries->size() << " record(s)";

                m_cache.insert(std::make_pair(key, entries));
            }
        }
        else
        {
            entries = it->second;
        }

        if (entries.get() == NULL)
        {
            LOG(ERROR) << "Can't find data";

            return entries;
        }

        return processEntries(entries, condition);
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Table select failed for:" << e.what();
    }

    return Entries::Ptr();
}

size_t dev::storage::MemoryTable::update(
    const std::string& key, Entry::Ptr entry, Condition::Ptr condition)
{
    try
    {
        LOG(DEBUG) << "Update MemoryTable: " << key;

        Entries::Ptr entries = std::make_shared<Entries>();

        auto it = m_cache.find(key);
        if (it == m_cache.end())
        {
            if (m_remoteDB.get() != NULL)
            {
                entries = m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo->name, key);

                LOG(DEBUG) << "AMOPDB selects:" << entries->size() << " record(s)";

                m_cache.insert(std::make_pair(key, entries));
            }
        }
        else
        {
            entries = it->second;
        }

        if (entries.get() == NULL)
        {
            LOG(ERROR) << "Can't find data";

            return 0;
        }

        Entries::Ptr updateEntries = processEntries(entries, condition);
        for (size_t i = 0; i < updateEntries->size(); ++i)
        {
            Entry::Ptr updateEntry = updateEntries->get(i);

            for (auto it : *(entry->fields()))
            {
                updateEntry->setField(it.first, it.second);
            }
        }

        entries->setDirty(true);

        return updateEntries->size();
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Access DB failed for:" << e.what();
    }

    return 0;
}

size_t dev::storage::MemoryTable::insert(const std::string& key, Entry::Ptr entry)
{
    try
    {
        LOG(DEBUG) << "Insert MemoryTable: " << key;

        Entries::Ptr entries = std::make_shared<Entries>();
        Condition::Ptr condition = std::make_shared<Condition>();

        auto it = m_cache.find(key);
        if (it == m_cache.end())
        {
            if (m_remoteDB.get() != NULL)
            {
                entries = m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo->name, key);

                LOG(DEBUG) << "AMOPDB selects:" << entries->size() << " record(s)";

                m_cache.insert(std::make_pair(key, entries));
            }
        }
        else
        {
            entries = it->second;
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
        LOG(ERROR) << "Access DB failed";
    }

    return 1;
}

size_t dev::storage::MemoryTable::remove(const std::string& key, Condition::Ptr condition)
{
    LOG(DEBUG) << "Remove MemoryTable data" << key;

    Entries::Ptr entries = std::make_shared<Entries>();

    auto it = m_cache.find(key);
    if (it == m_cache.end())
    {
        if (m_remoteDB.get() != NULL)
        {
            entries = m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo->name, key);

            LOG(DEBUG) << "AMOPDB selects:" << entries->size() << " record(s)";

            m_cache.insert(std::make_pair(key, entries));
        }
    }
    else
    {
        entries = it->second;
    }

    Entries::Ptr updateEntries = processEntries(entries, condition);
    for (size_t i = 0; i < updateEntries->size(); ++i)
    {
        Entry::Ptr removeEntry = updateEntries->get(i);

        removeEntry->setStatus(1);
    }

    entries->setDirty(true);

    return 1;
}

h256 dev::storage::MemoryTable::hash()
{
    bytes data;
    for (auto it : m_cache)
    {
        if (it.second->dirty())
        {
            data.insert(data.end(), it.first.begin(), it.first.end());
            for (size_t i = 0; i < it.second->size(); ++i)
            {
                if (it.second->get(i)->dirty())
                {
                    for (auto fieldIt : *(it.second->get(i)->fields()))
                    {
                        data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());

                        data.insert(data.end(), fieldIt.second.begin(), fieldIt.second.end());

                        // std::string status =
                        // boost::lexical_cast<std::string>(fieldIt.second->getStatus());
                        // data.insert(data.end(), status.begin(), status.end());
                    }
                }
            }
        }
    }

    if (data.empty())
    {
        return h256();
    }

    std::string str(data.begin(), data.end());

    bytesConstRef bR(data.data(), data.size());
    h256 hash = dev::sha256(bR);

    return hash;
}

void dev::storage::MemoryTable::clear()
{
    m_cache.clear();
}

std::map<std::string, Entries::Ptr>* dev::storage::MemoryTable::data()
{
    return &m_cache;
}

void dev::storage::MemoryTable::setStateStorage(Storage::Ptr amopDB)
{
    m_remoteDB = amopDB;
}

Entries::Ptr MemoryTable::processEntries(Entries::Ptr entries, Condition::Ptr condition)
{
    if (condition->getConditions()->empty())
    {
        return entries;
    }

    Entries::Ptr result = std::make_shared<Entries>();
    for (size_t i = 0; i < entries->size(); ++i)
    {
        Entry::Ptr entry = entries->get(i);
        if (processCondition(entry, condition))
        {
            result->addEntry(entry);
        }
    }

    return result;
}

bool dev::storage::MemoryTable::processCondition(Entry::Ptr entry, Condition::Ptr condition)
{
    try
    {
        for (auto it : *condition->getConditions())
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
        LOG(ERROR) << "Compare error:" << e.what();

        return false;
    }

    return true;
}

void MemoryTable::setBlockHash(h256 blockHash)
{
    m_blockHash = blockHash;
}

void MemoryTable::setBlockNum(int blockNum)
{
    m_blockNum = blockNum;
}
