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

#include "../protocol/ProtocolTypeDef.h"
#include "boost/algorithm/string.hpp"
#include "tbb/spin_mutex.h"
#include "tbb/spin_rw_mutex.h"
#include <bcos-utilities/Error.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <any>
#include <cstdlib>
#include <gsl/span>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define STORAGE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[STORAGE]"

namespace bcos
{
namespace storage
{
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
};

struct Condition
{
    Condition() = default;
    ~Condition() = default;
    void NE(const std::string& value) { m_conditions.emplace_back(Comparator::NE, value); }
    // string compare, "2" > "12"
    void GT(const std::string& value) { m_conditions.emplace_back(Comparator::GT, value); }
    void GE(const std::string& value) { m_conditions.emplace_back(Comparator::GE, value); }
    // string compare, "12" < "2"
    void LT(const std::string& value) { m_conditions.emplace_back(Comparator::LT, value); }
    void LE(const std::string& value) { m_conditions.emplace_back(Comparator::LE, value); }
    void limit(size_t start, size_t count) { m_limit = std::pair<size_t, size_t>(start, count); }

    std::pair<size_t, size_t> getLimit() const { return m_limit; }

    bool isValid(const std::string_view& key) const
    {  // all conditions must be satisfied
        for (auto& cond : m_conditions)
        {  // conditions should few, so not parallel check for now
            switch (cond.cmp)
            {
            case Comparator::NE:
                if (key == cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GT:
                if (key <= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GE:
                if (key < cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LT:
                if (key >= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LE:
                if (key > cond.value)
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

    enum class Comparator
    {
        EQ,
        NE,
        GT,
        GE,
        LT,
        LE,
    };
    struct cond
    {
        cond(Comparator _cmp, const std::string& _value) : cmp(_cmp), value(_value) {}
        Comparator cmp;
        std::string value;
    };
    std::vector<cond> m_conditions;
    std::pair<size_t, size_t> m_limit;
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

}  // namespace storage
}  // namespace bcos
