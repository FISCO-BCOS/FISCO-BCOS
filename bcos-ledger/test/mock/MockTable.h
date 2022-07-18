/**
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
 * @file MockTable.h
 * @author: kyonRay
 * @date 2021-05-07
 */
#pragma once

#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-ledger/libledger/utilities/Common.h"
#include <tbb/concurrent_unordered_map.h>

using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class MockTable : public Table
{
public:
    using Ptr = std::shared_ptr<MockTable>;

    explicit MockTable(std::string const& _tableName)
      : Table(nullptr, nullptr, 0), m_tableName(_tableName)
    {}

    std::shared_ptr<Entry> getRow(const std::string& _key) override
    {
        auto entry = m_fakeStorage[_key];
        if (entry)
        {
            return entry;
        }
        return nullptr;
    }

    bool setRow(const std::string& _key, const std::shared_ptr<Entry>& _entry) override
    {
        m_fakeStorage[_key] = _entry;
        return true;
    }

    std::vector<std::string> getPrimaryKeys(const Condition::Ptr&) const override
    {
        std::vector<std::string> keys;
        keys.reserve(m_fakeStorage.size());
        std::transform(m_fakeStorage.begin(), m_fakeStorage.end(), keys.begin(),
            [](auto pair) { return pair.first; });
        return keys;
    }

private:
    std::string m_tableName;
    std::unordered_map<std::string, Entry::Ptr> m_fakeStorage;
};

class MockErrorTableFactory : public TableStorage
{
public:
    explicit MockErrorTableFactory(storage::StorageInterface::Ptr _db)
      : TableStorage(_db, nullptr, -1)
    {
        m_sysTables.emplace_back(TableStorage::SYS_TABLES);
    }
    std::shared_ptr<TableInterface> openTable(const std::string& _tableName) override
    {
        if (std::find(m_sysTables.begin(), m_sysTables.end(), _tableName) != m_sysTables.end())
        {
            return TableFactory::openTable(_tableName);
        }
        else
        {
            return nullptr;
        }
    }
    bool createTable(const std::string& _tableName, const std::string& _keyField,
        const std::string& _valueFields) override
    {
        if (_tableName.at(0) == '/')
            m_sysTables.emplace_back(_tableName);
        return TableFactory::createTable(_tableName, _keyField, _valueFields);
    }
    std::pair<size_t, Error::Ptr> commit() override { return {0, std::make_shared<Error>(-1, "")}; }

    std::vector<std::string> m_sysTables;
};
}  // namespace bcos::test
