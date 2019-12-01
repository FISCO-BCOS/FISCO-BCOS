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
/** @file Table.cpp
 *  @author ancelmo
 *  @date 20180921
 */
/*
 * Table.cpp
 *
 *  Created on: 2018.4.27
 *      Author: mo nan
 */

#include "StorageException.h"

#include "Common.h"
#include "Table.h"
#include <libdevcore/Common.h>
#include <tbb/pipeline.h>
#include <tbb/tbb_thread.h>
#include <boost/lexical_cast.hpp>

using namespace dev::storage;
using namespace std;

Entry::Entry() : m_data(std::make_shared<EntryData>())
{
    m_data->m_refCount = 1;
    // m_data->m_fields.insert(std::make_pair(STATUS, "0"));
}

Entry::~Entry()
{
    RWMutexScoped lock(m_data->m_mutex, true);
    if (m_data->m_refCount > 0)
    {
        --m_data->m_refCount;
    }
}

uint64_t Entry::getID() const
{
    RWMutexScoped lock(m_data->m_mutex, false);

    return m_ID;
}

void Entry::setID(uint64_t id)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_ID = id;

    m_dirty = true;
}

void Entry::setID(const std::string& id)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_ID = boost::lexical_cast<uint64_t>(id);

    m_dirty = true;
}

dev::bytesConstRef Entry::getFieldConst(const std::string& key) const
{
    RWMutexScoped lock(m_data->m_mutex, false);

    auto it = m_data->m_fields.find(key);

    if (it != m_data->m_fields.end())
    {
        return dev::bytesConstRef(it->second);
    }

    STORAGE_LOG(ERROR) << LOG_BADGE("Entry") << LOG_DESC("can't find key") << LOG_KV("key", key);
    return dev::bytesConstRef();
}

std::string Entry::getField(const std::string& key) const
{
    RWMutexScoped lock(m_data->m_mutex, false);

    auto it = m_data->m_fields.find(key);

    if (it != m_data->m_fields.end())
    {
        return it->second;
    }

    STORAGE_LOG(ERROR) << LOG_BADGE("Entry") << LOG_DESC("can't find key") << LOG_KV("key", key);
    return "";
}

void Entry::setField(const std::string& key, const std::string& value)
{
    auto lock = checkRef();

    auto it = m_data->m_fields.find(key);

    if (it != m_data->m_fields.end())
    {
        m_capacity -= (key.size() + it->second.size());
        it->second = value;
        m_capacity += (key.size() + value.size());
    }
    else
    {
        m_data->m_fields.insert(std::make_pair(key, value));
        m_capacity += (key.size() + value.size());
    }

    assert(m_capacity >= 0);
    m_dirty = true;
}

void Entry::setField(const std::string& key, const byte* value, size_t size)
{
    auto lock = checkRef();

    auto it = m_data->m_fields.find(key);

    if (it != m_data->m_fields.end())
    {
        m_capacity -= (key.size() + it->second.size());
        it->second.assign((char*)value, (char*)value + size);
        m_capacity += (key.size() + size);
    }
    else
    {
        m_data->m_fields.emplace(key, std::string((char*)value, size));
        // m_data->m_fields.insert(std::make_pair(key, std::string(value, size)));
        m_capacity += (key.size() + size);
    }

    assert(m_capacity >= 0);
    m_dirty = true;
}

size_t Entry::getTempIndex() const
{
    RWMutexScoped lock(m_data->m_mutex, false);

    return m_tempIndex;
}

void Entry::setTempIndex(size_t index)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_tempIndex = index;
}

std::map<std::string, std::string>::const_iterator Entry::find(const std::string& key) const
{
    return m_data->m_fields.find(key);
}

std::map<std::string, std::string>::const_iterator Entry::begin() const
{
    return m_data->m_fields.begin();
}

std::map<std::string, std::string>::const_iterator Entry::end() const
{
    return m_data->m_fields.end();
}

size_t Entry::size() const
{
    return m_data->m_fields.size();
}

int Entry::getStatus() const
{
    RWMutexScoped lock(m_data->m_mutex, false);

    return m_status;
}

void Entry::setStatus(int status)
{
    auto lock = checkRef();

#if 0
    auto it = m_data->m_fields.find(STATUS);
    if (it == m_data->m_fields.end())
    {
        m_data->m_fields.insert(std::make_pair(STATUS, boost::lexical_cast<std::string>(status)));
    }
    else
    {
        it->second = boost::lexical_cast<std::string>(status);
    }
#endif

    m_status = status;
    m_dirty = true;
}

void Entry::setStatus(const std::string& status)
{
    auto lock = checkRef();

#if 0
    auto it = m_data->m_fields.find(STATUS);
    if (it == m_data->m_fields.end())
    {
        m_data->m_fields.insert(std::make_pair(STATUS, status));
    }
    else
    {
        it->second = status;
    }
#endif

    m_status = boost::lexical_cast<int>(status);
    m_dirty = true;
}

uint32_t Entry::num() const
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_num;
}

void Entry::setNum(uint32_t num)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_num = num;
    m_dirty = true;
}

void Entry::setNum(const std::string& id)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_num = boost::lexical_cast<uint32_t>(id);
    m_dirty = true;
}

bool Entry::dirty() const
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_dirty;
}

void Entry::setDirty(bool dirty)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_dirty = dirty;
}

bool Entry::force() const
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_force;
}

void Entry::setForce(bool force)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_force = force;
}

bool Entry::deleted() const
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_deleted;
}

void Entry::setDeleted(bool deleted)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    m_deleted = deleted;
}

ssize_t Entry::capacity() const
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_capacity;
}

void Entry::copyFrom(Entry::ConstPtr entry)
{
    RWMutexScoped lock(m_data->m_mutex, true);

    RWMutexScoped lock2;
    while (true)
    {
        auto locked = lock2.try_acquire(entry->m_data->m_mutex, true);
        if (!locked)
        {
            if (m_data == entry->m_data)
            {
                return;
            }
            else
            {
                tbb::this_tbb_thread::yield();
            }
        }
        else
        {
            break;
        }
    }

    m_ID = entry->m_ID;
    m_status = entry->m_status;
    m_tempIndex = entry->m_tempIndex;
    m_num = entry->m_num;
    m_dirty = entry->m_dirty;
    m_force = entry->m_force;
    m_deleted = entry->m_deleted;
    m_capacity = entry->m_capacity;

    auto oldData = m_data;
    m_data->m_refCount -= 1;

    m_data = entry->m_data;
    lock.release();

    m_data->m_refCount += 1;
}

ssize_t Entry::refCount()
{
    RWMutexScoped lock(m_data->m_mutex, false);
    return m_data->m_refCount;
}

std::shared_ptr<Entry::RWMutexScoped> Entry::lock(bool write)
{
    auto lock = std::make_shared<RWMutexScoped>(m_data->m_mutex, write);

    return lock;
}

std::shared_ptr<tbb::spin_rw_mutex::scoped_lock> Entry::checkRef()
{
    auto lock = std::make_shared<RWMutexScoped>(m_data->m_mutex, true);

    if (m_data->m_refCount > 1)
    {
        auto m_oldData = m_data;
        m_data = std::make_shared<EntryData>();

        m_data->m_refCount = 1;
        m_data->m_fields = m_oldData->m_fields;

        m_oldData->m_refCount -= 1;

        assert(m_oldData->m_refCount >= 0);
        lock = std::make_shared<tbb::spin_rw_mutex::scoped_lock>(m_data->m_mutex, true);
    }

    return lock;
}

bool EntryLessNoLock::operator()(const Entry::Ptr& lhs, const Entry::Ptr& rhs) const
{
    if (lhs->getID() != rhs->getID())
    {
        return lhs->getID() < rhs->getID();
    }

    auto lhsStr = lhs->getField(m_tableInfo->key);
    auto rhsStr = rhs->getField(m_tableInfo->key);
    if (lhsStr != rhsStr)
    {
        return lhsStr < rhsStr;
    }

    auto lSize = lhs->size();
    auto rSize = rhs->size();
    if (lSize != rSize)
    {
        return lSize < rSize;
    }

    for (auto lIter = lhs->begin(), rIter = rhs->begin();
         lIter != lhs->end() && rIter != rhs->end();)
    {
        if (lIter->first != rIter->first)
        {
            return lIter->first < rIter->first;
        }

        if (lIter->second != rIter->second)
        {
            return lIter->second < rIter->second;
        }

        ++lIter;
        ++rIter;
    }

    return false;
}

Entries::Vector::const_iterator Entries::begin() const
{
    return m_entries.begin();
}

Entries::Vector::const_iterator Entries::end() const
{
    return m_entries.end();
}

Entries::Vector::iterator Entries::begin()
{
    return m_entries.begin();
}

Entries::Vector::iterator Entries::end()
{
    return m_entries.end();
}

Entries::Vector::reference Entries::operator[](Vector::size_type index)
{
    return m_entries[index];
}

size_t Entries::size() const
{
    return m_entries.size();
}

void Entries::resize(size_t n)
{
    m_entries.resize(n);
}

Entry::ConstPtr Entries::get(size_t i) const
{
    if (m_entries.size() <= i)
    {
        BOOST_THROW_EXCEPTION(
            StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i)));

        return Entry::Ptr();
    }

    return m_entries[i];
}

Entry::Ptr Entries::get(size_t i)
{
    if (m_entries.size() <= i)
    {
        BOOST_THROW_EXCEPTION(
            StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i)));

        return Entry::Ptr();
    }

    return m_entries[i];
}

size_t Entries::addEntry(Entry::Ptr entry)
{
    size_t index = m_entries.push_back(entry) - m_entries.begin();
    m_dirty = true;

    return index;
}

void Entries::removeEntry(size_t index)
{
    if (index < m_entries.size())
    {
        m_entries.at(index)->setDeleted(true);
        m_entries.at(index)->setStatus(1);
    }
}

bool Entries::dirty() const
{
    return m_dirty;
}

void Entries::setDirty(bool dirty)
{
    m_dirty = dirty;
}

void Entries::shallowFrom(Entries::Ptr entries)
{
    m_entries = entries->m_entries;
    m_dirty = entries->m_dirty;
}

#if 0
size_t ConcurrentEntries::size() const
{
    return m_entries.size();
}

Entry::Ptr ConcurrentEntries::get(size_t i)
{
    if (m_entries.size() <= i)
    {
        BOOST_THROW_EXCEPTION(
            StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i)));

        return Entry::Ptr();
    }

    // at() is more safe than operator[](), at() will check whether there is a memory overread
    return m_entries.at(i);
}

typename ConcurrentEntries::EntriesIter ConcurrentEntries::addEntry(Entry::Ptr entry)
{
    auto iter = m_entries.push_back(entry);
    m_dirty = true;
    return iter;
}

bool ConcurrentEntries::dirty() const
{
    return m_dirty;
}

void ConcurrentEntries::setDirty(bool dirty)
{
    m_dirty = dirty;
}
#endif

void Condition::EQ(const std::string& key, const std::string& value)
{
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::eq, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.left.first = true;
        it->second.left.second = value;
        it->second.right.first = true;
        it->second.right.second = value;
    }
    else
    {
        m_conditions.insert(
            std::make_pair(key, Range(std::make_pair(true, value), std::make_pair(true, value))));
    }
}

void Condition::NE(const std::string& key, const std::string& value)
{
    // not equal contains two area
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::ne, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.right.first = false;
        it->second.right.second = value;
        it->second.left.first = false;
        it->second.left.second = value;
    }
    else
    {
        m_conditions.insert(
            std::make_pair(key, Range(std::make_pair(false, value), std::make_pair(false, value))));
    }
}

void Condition::GT(const std::string& key, const std::string& value)
{
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::gt, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.left.first = false;
        it->second.left.second = value;
    }
    else
    {
        m_conditions.insert(std::make_pair(
            key, Range(std::make_pair(false, value), std::make_pair(false, UNLIMITED))));
    }
}

void Condition::GE(const std::string& key, const std::string& value)
{
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::ge, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.left.first = true;
        it->second.left.second = value;
    }
    else
    {
        m_conditions.insert(std::make_pair(
            key, Range(std::make_pair(true, value), std::make_pair(false, UNLIMITED))));
    }
}

void Condition::LT(const std::string& key, const std::string& value)
{
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::lt, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.right.first = false;
        it->second.right.second = value;
    }
    else
    {
        m_conditions.insert(std::make_pair(
            key, Range(std::make_pair(false, UNLIMITED), std::make_pair(false, value))));
    }
}

void Condition::LE(const std::string& key, const std::string& value)
{
    // m_conditions.insert(std::make_pair(key, std::make_pair(Op::le, value)));
    auto it = m_conditions.find(key);
    if (it != m_conditions.end())
    {
        it->second.right.first = true;
        it->second.right.second = value;
    }
    else
    {
        m_conditions.insert(std::make_pair(
            key, Range(std::make_pair(false, UNLIMITED), std::make_pair(true, value))));
    }
}

void Condition::limit(int64_t count)
{
    limit(0, count);
}

void Condition::limit(int64_t offset, int64_t count)
{
    m_offset = offset;
    m_count = count;
}

int Condition::getOffset()
{
    return m_offset;
}

int Condition::getCount()
{
    return m_count;
}

std::map<std::string, Condition::Range>::const_iterator Condition::begin() const
{
    return m_conditions.begin();
}

std::map<std::string, Condition::Range>::const_iterator Condition::end() const
{
    return m_conditions.end();
}

bool Condition::empty()
{
    return m_conditions.empty();
}

bool Condition::process(Entry::Ptr entry)
{
    try
    {
        if (entry->getStatus() == Entry::Status::DELETED || entry->deleted())
        {
            return false;
        }

        if (!m_conditions.empty())
        {
            for (auto it : m_conditions)
            {
                if (!isHashField(it.first))
                {
                    continue;
                }
                auto fieldIt = entry->find(it.first);
                if (fieldIt != entry->end())
                {
                    if (it.second.left.second == it.second.right.second && it.second.left.first &&
                        it.second.right.first)
                    {
                        if (it.second.left.second == fieldIt->second)
                        {
                            // point hited
                            continue;
                        }
                        else
                        {
                            // point missed
                            return false;
                        }
                    }

                    if (it.second.left.second != UNLIMITED)
                    {
                        auto lhs = boost::lexical_cast<int64_t>(it.second.left.second);
                        auto rhs = (int64_t)0;
                        if (!fieldIt->second.empty())
                        {
                            rhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }

                        if (it.second.left.first)
                        {
                            if (!(lhs <= rhs))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            if (!(lhs < rhs))
                            {
                                return false;
                            }
                        }
                    }

                    if (it.second.right.second != UNLIMITED)
                    {
                        auto lhs = boost::lexical_cast<int64_t>(it.second.right.second);
                        auto rhs = (int64_t)0;
                        if (!fieldIt->second.empty())
                        {
                            rhs = boost::lexical_cast<int64_t>(fieldIt->second);
                        }

                        if (it.second.right.first)
                        {
                            if (!(lhs >= rhs))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            if (!(lhs > rhs))
                            {
                                return false;
                            }
                        }
                    }
                }
                else
                {
                    return false;
                }
            }
        }
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("Condition") << LOG_DESC("process error")
                           << LOG_KV("msg", boost::diagnostic_information(e));
        return false;
    }

    return true;
}

bool Condition::graterThan(Condition::Ptr condition)
{
    (void)condition;

    return true;
}

bool Condition::related(Condition::Ptr condition)
{
    (void)condition;

    return false;
}

TableInfo::Ptr dev::storage::getSysTableInfo(const string& tableName)
{
    auto tableInfo = make_shared<storage::TableInfo>();
    tableInfo->name = tableName;
    if (tableName == SYS_CONSENSUS)
    {
        tableInfo->key = "name";
        tableInfo->fields = vector<string>{"type", "node_id", "enable_num"};
    }
    else if (tableName == SYS_TABLES)
    {
        tableInfo->key = "table_name";
        tableInfo->fields = vector<string>{"key_field", "value_field"};
    }
    else if (tableName == SYS_ACCESS_TABLE)
    {
        tableInfo->key = "table_name";
        tableInfo->fields = vector<string>{"address", "enable_num"};
    }
    else if (tableName == SYS_CURRENT_STATE)
    {
        tableInfo->key = SYS_KEY;
        tableInfo->fields = vector<string>{"value"};
        tableInfo->enableConsensus = false;
    }
    else if (tableName == SYS_NUMBER_2_HASH)
    {
        tableInfo->key = "number";
        tableInfo->fields = vector<string>{"value"};
    }
    else if (tableName == SYS_TX_HASH_2_BLOCK)
    {
        tableInfo->key = "hash";
        tableInfo->fields = vector<string>{"value", "index"};
        tableInfo->enableConsensus = false;
        tableInfo->enableCache = false;
    }
    else if (tableName == SYS_HASH_2_BLOCK)
    {
        tableInfo->key = "hash";
        tableInfo->fields = vector<string>{"value"};
        tableInfo->enableConsensus = false;
    }
    else if (tableName == SYS_CNS)
    {
        tableInfo->key = "name";
        tableInfo->fields = vector<string>{"version", "address", "abi"};
    }
    else if (tableName == SYS_CONFIG)
    {
        tableInfo->key = "key";
        tableInfo->fields = vector<string>{"value", "enable_num"};
    }
    else if (tableName == SYS_BLOCK_2_NONCES)
    {
        tableInfo->key = "number";
        tableInfo->fields = vector<string>{SYS_VALUE};
        tableInfo->enableConsensus = false;
    }
    return tableInfo;
}
