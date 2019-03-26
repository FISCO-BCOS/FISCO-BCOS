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

using namespace dev::storage;

Entry::Entry()
{
    m_fields.insert(std::make_pair(ID_FIELD, "0"));
    m_fields.insert(std::make_pair(STATUS, "0"));
}

uint32_t Entry::getID() const
{
    // dev::ReadGuard l(x_fields);
    auto it = m_fields.find(ID_FIELD);
    if (it == m_fields.end())
    {
        return 0;
    }
    else
    {
        return boost::lexical_cast<uint32_t>(it->second);
    }
}

void Entry::setID(uint32_t id)
{
    auto it = m_fields.find(STATUS);
    if (it == m_fields.end())
    {
        m_fields.insert(std::make_pair(ID_FIELD, boost::lexical_cast<std::string>(id)));
    }
    else
    {
        it->second = boost::lexical_cast<std::string>(id);
    }

    m_dirty = true;
}

std::string Entry::getField(const std::string& key) const
{
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

size_t Entry::getTempIndex() const
{
    return m_tempIndex;
}

void Entry::setTempIndex(size_t index)
{
    m_tempIndex = index;
}

std::map<std::string, std::string>* Entry::fields()
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

size_t Entries::size() const
{
    return m_entries.size();
}

Entry::Ptr Entries::get(size_t i)
{
    if (m_entries.size() <= i)
    {
        throw StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i));

        return Entry::Ptr();
    }

    return m_entries[i];
}

void Entries::addEntry(Entry::Ptr entry)
{
    m_entries.push_back(entry);
    m_dirty = true;
}

void Entries::removeEntry(size_t index)
{
    m_entries.erase(m_entries.begin() + index);
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

bool Table::processCondition(Entry::Ptr entry, Condition::Ptr condition)
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
