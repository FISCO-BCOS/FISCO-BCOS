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
 *  Created on: 2018年4月27日
 *      Author: mo nan
 */

#include "StorageException.h"

#include "Common.h"
#include "Table.h"
#include <libdevcore/easylog.h>
#include <boost/lexical_cast.hpp>
#include <map>

using namespace dev::storage;

Entry::Entry()
{
    // status required
    m_fields.insert(std::make_pair(STATUS, "0"));
}

std::string Entry::getField(const std::string& key) const
{
    // dev::ReadGuard l(x_fields);
    auto it = m_fields.find(key);

    if (it != m_fields.end())
    {
        return it->second;
    }
    STORAGE_LOG(ERROR) << LOG_BADGE("Entry") << LOG_DESC("can't find key") << LOG_KV("key", key);

    return "";
}

void Entry::setField(const std::string& key, const std::string& value)
{
    // dev::WriteGuard l(x_fields);
    auto it = m_fields.find(key);

    if (it != m_fields.end())
    {
        it->second = value;
    }
    else
    {
        m_fields.insert(std::make_pair(key, value));
    }

    m_dirty = true;
}

typename Entry::FieldsMap* Entry::fields()
{
    return &m_fields;
}

uint32_t Entry::getStatus()
{
    // dev::ReadGuard l(x_fields);
    auto it = m_fields.find(STATUS);
    if (it == m_fields.end())
    {
        return 0;
    }
    else
    {
        return boost::lexical_cast<uint32_t>(it->second);
    }
}

void Entry::setStatus(int status)
{
    // dev::WriteGuard l(x_fields);
    auto it = m_fields.find(STATUS);
    if (it == m_fields.end())
    {
        m_fields.insert(std::make_pair(STATUS, boost::lexical_cast<std::string>(status)));
    }
    else
    {
        it->second = boost::lexical_cast<std::string>(status);
    }

    m_dirty = true;
}

bool Entry::dirty() const
{
    return m_dirty;
}

void Entry::setDirty(bool dirty)
{
    m_dirty = dirty;
}

Entry::Ptr Entries::get(size_t i)
{
    // dev::ReadGuard l(x_entries);
    if (m_entries.size() <= i)
    {
        throw StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i));

        return Entry::Ptr();
    }

    return m_entries[i];
}

size_t Entries::size() const
{
    // dev::ReadGuard l(x_entries);
    return m_entries.size();
}

void Entries::addEntry(Entry::Ptr entry)
{
    // dev::WriteGuard l(x_entries);
    m_entries.push_back(entry);
    m_dirty = true;
}

void Entries::removeEntry(size_t index)
{
    // dev::WriteGuard l(x_entries);
    // m_entries.erase(m_entries.begin() + index);
}

bool Entries::dirty() const
{
    return m_dirty;
}

void Entries::setDirty(bool dirty)
{
    m_dirty = dirty;
}

void Condition::EQ(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::eq, value)));
}

void Condition::NE(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::ne, value)));
}

void Condition::GT(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::gt, value)));
}

void Condition::GE(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::ge, value)));
}

void Condition::LT(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::lt, value)));
}

void Condition::LE(const std::string& key, const std::string& value)
{
    m_conditions.insert(std::make_pair(key, std::make_pair(Op::le, value)));
}

void Condition::limit(size_t count)
{
    limit(0, count);
}

void Condition::limit(size_t offset, size_t count)
{
    m_offset = offset;
    m_count = count;
}

std::map<std::string, std::pair<Condition::Op, std::string> >* Condition::getConditions()
{
    return &m_conditions;
}

Entry::Ptr Table::newEntry()
{
    return std::make_shared<Entry>();
}
Condition::Ptr Table::newCondition()
{
    return std::make_shared<Condition>();
}
