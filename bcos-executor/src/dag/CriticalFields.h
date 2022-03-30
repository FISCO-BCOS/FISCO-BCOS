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
 */
/**
 * @brief : Critical fields analysing
 * @author: jimmyshi
 * @date: 2022-1-14
 */


#pragma once
#include <functional>
#include <map>
#include <unordered_map>
#include <set>
#include <vector>
#include <boost/container_hash/hash.hpp>

namespace bcos
{
namespace executor
{
namespace critical
{
using ID = uint32_t;
static const ID INVALID_ID = (ID(0) - 1);

using OnConflictHandler = std::function<void(ID, ID)>;   // conflict from -> to
using OnFirstConflictHandler = std::function<void(ID)>;  // conflict
using OnEmptyConflictHandler = std::function<void(ID)>;  // conflict
using OnAllConflictHandler = std::function<void(ID)>;    // conflict

class CriticalFieldsInterface
{
public:
    using Ptr = std::shared_ptr<CriticalFieldsInterface>;

    virtual size_t size() = 0;

    virtual bool contains(size_t id) = 0;

    virtual void traverseDag(OnConflictHandler const& _onConflict,
        OnFirstConflictHandler const& _onFirstConflict,
        OnEmptyConflictHandler const& _onEmptyConflict,
        OnAllConflictHandler const& _onAllConflict) = 0;
};

class CriticalFields : public virtual CriticalFieldsInterface
{
public:
    using Ptr = std::shared_ptr<CriticalFields>;
    using CriticalField = std::vector<std::vector<uint8_t>>;
    using CriticalFieldPtr = std::shared_ptr<CriticalField>;

    CriticalFields(size_t _size) : m_criticals(std::vector<CriticalFieldPtr>(_size)) {}
    virtual ~CriticalFields() {}

    size_t size() override { return m_criticals.size(); }
    bool contains(size_t id) override { return id < size() && get(id) != nullptr; };
    void put(size_t _id, CriticalFieldPtr _criticalField) { m_criticals[_id] = _criticalField; }
    CriticalFieldPtr get(size_t _id) { return m_criticals[_id]; }

    void traverseDag(OnConflictHandler const& _onConflict,
        OnFirstConflictHandler const& _onFirstConflict,
        OnEmptyConflictHandler const& _onEmptyConflict,
        OnAllConflictHandler const& _onAllConflict) override
    {
        auto dependencies = std::unordered_map<std::vector<uint8_t>, std::vector<size_t>, boost::hash<std::vector<uint8_t>>>();

        for (ID id = 0; id < m_criticals.size(); ++id)
        {
            auto criticals = m_criticals[id];

            if (criticals == nullptr)
            {
                _onAllConflict(id);
            }
            else if (criticals->empty())
            {
                _onEmptyConflict(id);
            }
            else if (!criticals->empty())
            {
                // Get conflict parent's id set
                std::set<ID> pIds;
                for (auto const& c : *criticals)
                {
                    auto& ids = dependencies[c];
                    for (auto pId : ids)
                    {
                        pIds.insert(pId);
                    }
                    ids.push_back(id);
                }

                if (pIds.empty())
                {
                    _onFirstConflict(id);
                }
                else
                {
                    for (ID pId : pIds)
                    {
                        _onConflict(pId, id);
                    }
                }
            }
            else
            {
                continue;
            }
        }
    };

private:
    std::vector<CriticalFieldPtr> m_criticals;
};
}  // namespace critical
}  // namespace executor
}  // namespace bcos
