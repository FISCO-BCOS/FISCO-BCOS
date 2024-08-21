/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief interface of Table
 * @file Table.h
 * @author: xingqiangbai
 * @date: 2021-04-07
 */
#pragma once

#include <bcos-utilities/Error.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstdlib>
#include <gsl/span>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define STORAGE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[STORAGE]"

namespace bcos::storage
{

const std::string ROCKSDB = "rocksDB";
const std::string TiKV = "TiKV";

enum StorageError
{
    UnknownError = -60000,
    TableNotExists,
    SystemTableNotExists,
    TableExists,
    UnknownEntryType,
    ReadError,
    WriteError,
    EmptyStorage,
    ReadOnly,
    DatabaseError,
    DatabaseRetryable,
    TryLockFailed,
    TimestampMismatch,
};

struct Condition
{
    Condition() = default;
    ~Condition() = default;

    // string compare, "2" > "12"
    void GT(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }

        auto [it, inserted] = m_conditions.try_emplace(Comparator::GT, value);
        if (!inserted && value > it->second)
        {
            it->second = value;
        }

        // 1. x > a && x < b && a >= b (not exist)
        // 2. x > a && x <= b && a >= b (not exist)
        auto it1 = m_conditions.find(Comparator::LT);
        auto it2 = m_conditions.find(Comparator::LE);
        if ((it1 != m_conditions.end() && it->second >= it1->second) ||
            (it2 != m_conditions.end() && it->second >= it2->second))
        {
            m_hasConflictCond = true;
        }
    }

    void GE(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }

        auto [it, inserted] = m_conditions.try_emplace(Comparator::GE, value);
        if (!inserted && value > it->second)
        {
            it->second = value;
        }

        // 1. x >= a && x < b && a >= b (not exist)
        // 2. x >= a && x <= b && a > b (not exist)
        auto it1 = m_conditions.find(Comparator::LT);
        auto it2 = m_conditions.find(Comparator::LE);
        if ((it1 != m_conditions.end() && it->second >= it1->second) ||
            (it2 != m_conditions.end() && it->second > it2->second))
        {
            m_hasConflictCond = true;
        }
    }

    // string compare, "12" < "2"
    void LT(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }

        auto [it, inserted] = m_conditions.try_emplace(Comparator::LT, value);
        if (!inserted && value < it->second)
        {
            it->second = value;
        }

        // 1. x > a  && x < b && a >= b (not exist)
        // 2. x >= a && x < b && a >= b (not exist)
        auto it1 = m_conditions.find(Comparator::GT);
        auto it2 = m_conditions.find(Comparator::GE);
        if ((it1 != m_conditions.end() && it->second <= it1->second) ||
            (it2 != m_conditions.end() && it->second <= it2->second))
        {
            m_hasConflictCond = true;
        }
    }

    void LE(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }
        auto [it, inserted] = m_conditions.try_emplace(Comparator::LE, value);
        if (!inserted && value < it->second)
        {
            it->second = value;
        }

        // 1. x > a  && x <= b && a >= b (not exist)
        // 2. x >= a && x <= b && a > b (not exist)
        auto it1 = m_conditions.find(Comparator::GT);
        auto it2 = m_conditions.find(Comparator::GE);
        if ((it1 != m_conditions.end() && it->second <= it1->second) ||
            (it2 != m_conditions.end() && it->second < it2->second))
        {
            m_hasConflictCond = true;
        }
    }

    void EQ(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }
        // x == a  <==>  x >= a && x <= a
        // Convert single point queries to range queries
        GE(value);
        LE(value);
    }

    void NE(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }
        m_repeatableConditions.emplace_back(Comparator::NE, value);
    }

    void startsWith(const std::string& value, bool updateGE = true)
    {
        if (m_hasConflictCond)
        {
            return;
        }

        auto [it, inserted] = m_conditions.try_emplace(Comparator::STARTS_WITH, value);
        if (inserted)
        {
            if (updateGE)
            {
                GE(value);
            }
            return;
        }

        // it->second = abc, value = abcd
        if (value.starts_with(it->second))
        {
            it->second = value;
            if (updateGE)
            {
                GE(value);
            }
        }
        // it->second = abcf, value = abcd
        else if (!it->second.starts_with(value))
        {
            m_hasConflictCond = true;
        }
        // it->second = abcd, value = abc
    }

    void endsWith(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }

        auto [it, inserted] = m_conditions.try_emplace(Comparator::ENDS_WITH, value);
        if (inserted)
        {
            return;
        }

        // it->second = def, value = cdef
        if (value.ends_with(it->second))
        {
            it->second = value;
        }
        // it->second = def, value = deh
        else if (!it->second.ends_with(value))
        {
            m_hasConflictCond = true;
        }
        // it->second = cdef, value = def
    }

    void contains(const std::string& value)
    {
        if (m_hasConflictCond)
        {
            return;
        }
        m_repeatableConditions.emplace_back(Comparator::CONTAINS, value);
    }

    void limit(size_t start, size_t count) { m_limit = std::pair<size_t, size_t>(start, count); }

    std::pair<size_t, size_t> getLimit() const { return m_limit; }

    template <typename Container>
    static bool isValid(const std::string_view& key, const Container& conds)
    {
        // all conditions must be satisfied
        for (auto& it : conds)
        {  // conditions should few, so not parallel check for now
            switch (it.first)
            {
            case Comparator::EQ:
                if (key != it.second)
                {
                    return false;
                }
                break;
            case Comparator::NE:
                if (key == it.second)
                {
                    return false;
                }
                break;
            case Comparator::GT:
                if (key <= it.second)
                {
                    return false;
                }
                break;
            case Comparator::GE:
                if (key < it.second)
                {
                    return false;
                }
                break;
            case Comparator::LT:
                if (key >= it.second)
                {
                    return false;
                }
                break;
            case Comparator::LE:
                if (key > it.second)
                {
                    return false;
                }
                break;
            case Comparator::STARTS_WITH:
                if (!key.starts_with(it.second))
                {
                    return false;
                }
                break;
            case Comparator::ENDS_WITH:
                if (!key.ends_with(it.second))
                {
                    return false;
                }
                break;
            case Comparator::CONTAINS:
                if (key.find(it.second) == std::string::npos)
                {
                    return false;
                }
                break;
            default:
                // undefined Comparator
                break;
            }
        }
        return true;
    }

    bool isValid(const std::string_view& key) const
    {
        // If there are conflicting conditions, it means that no key can meet the conditions.
        if (m_hasConflictCond)
        {
            return false;
        }
        return isValid(key, m_conditions) && isValid(key, m_repeatableConditions);
    }

    enum class Comparator : uint8_t
    {
        GT = 0,
        GE = 1,
        LT = 2,
        LE = 3,
        EQ = 4,
        NE = 5,
        STARTS_WITH = 6,
        ENDS_WITH = 7,
        CONTAINS = 8
    };

    static std::string toString(const std::pair<Comparator, std::string>& cond)
    {
        std::string cmpStr;
        switch (cond.first)
        {
        case Comparator::EQ:
            cmpStr = "EQ";
            break;
        case Comparator::NE:
            cmpStr = "NE";
            break;
        case Comparator::GT:
            cmpStr = "GT";
            break;
        case Comparator::GE:
            cmpStr = "GE";
            break;
        case Comparator::LT:
            cmpStr = "NE";
            break;
        case Comparator::LE:
            cmpStr = "LE";
            break;
        case Comparator::STARTS_WITH:
            cmpStr = "STARTS_WITH";
            break;
        case Comparator::ENDS_WITH:
            cmpStr = "ENDS_WITH";
            break;
        case Comparator::CONTAINS:
            cmpStr = "CONTAINS";
            break;
        }
        return cmpStr + " " + cond.second;
    }

    std::map<Comparator, std::string> m_conditions;
    std::vector<std::pair<Comparator, std::string>> m_repeatableConditions;
    std::pair<size_t, size_t> m_limit;
    bool m_hasConflictCond = false;

    // this method only for trace log
    std::string toString() const
    {
        std::stringstream ss;
        ss << "Condition: ";
        for (const auto& cond : m_conditions)
        {
            ss << toString(cond) << ";";
        }

        for (const auto& cond : m_repeatableConditions)
        {
            ss << toString(cond) << ";";
        }
        ss << "limit start: " << m_limit.first << "limit count: " << m_limit.second;
        return ss.str();
    }
};

class TableInfo
{
public:
    using Ptr = std::shared_ptr<TableInfo>;
    using ConstPtr = std::shared_ptr<const TableInfo>;

    TableInfo(std::string name, std::vector<std::string> fields)
      : m_name(std::move(name)), m_fields(std::move(fields))
    {
        m_order.reserve(m_fields.size());
        for (size_t i = 0; i < m_fields.size(); ++i)
        {
            m_order.push_back({m_fields[i], i});
        }
        std::sort(m_order.begin(), m_order.end(),
            [](auto&& lhs, auto&& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
    }

    std::string_view name() const { return m_name; }

    const std::vector<std::string>& fields() const { return m_fields; }

    size_t fieldIndex(const std::string_view& field) const
    {
        auto it = std::lower_bound(m_order.begin(), m_order.end(), field,
            [](auto&& lhs, auto&& rhs) { return std::get<0>(lhs) < rhs; });
        if (it != m_order.end() && std::get<0>(*it) == field)
        {
            return std::get<1>(*it);
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(-1, std::string("Can't find field: ") + std::string(field)));
        }
    }

private:
    std::string m_name;
    std::vector<std::string> m_fields;
    std::vector<std::tuple<std::string_view, size_t>> m_order;

private:
    void* operator new(size_t s) { return malloc(s); };
    void operator delete(void* p) { free(p); };
};

const char* const TABLE_KEY_SPLIT = ":";

inline std::string toDBKey(const std::string_view& tableName, const std::string_view& key)
{
    std::string dbKey;
    dbKey.reserve(tableName.size() + 1 + key.size());
    dbKey.append(tableName).append(TABLE_KEY_SPLIT).append(key);
    return dbKey;
}

}  // namespace bcos::storage
