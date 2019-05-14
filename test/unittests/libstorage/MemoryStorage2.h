/**
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
 *
 * @brief
 *
 * @file ExecuteVMTest.cpp
 * @author: xingqiangbai
 * @date 2018-10-25
 */

#pragma once

#include "libstorage/Storage.h"

namespace dev
{
namespace storage
{
class MemoryStorage2 : public Storage
{
public:
    typedef std::shared_ptr<MemoryStorage> Ptr;

    virtual ~MemoryStorage(){};

    std::vector<size_t> processEntries(Entries::Ptr entries, Condition::Ptr condition)
    {
        std::vector<size_t> indexes;
        indexes.reserve(entries->size());
        if (condition->getConditions()->empty())
        {
            for (size_t i = 0; i < entries->size(); ++i)
                indexes.emplace_back(i);
            return indexes;
        }

        for (size_t i = 0; i < entries->size(); ++i)
        {
            Entry::Ptr entry = entries->get(i);
            if (processCondition(entry, condition))
            {
                indexes.push_back(i);
            }
        }

        return indexes;
    }

    bool processCondition(Entry::Ptr entry, Condition::Ptr condition)
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
            return false;
        }

        return true;
    }

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)hash;
        (void)num;
        auto it = tableData.find(tableInfo->name);

        if (it != tableData.end())
        {
            condition->EQ(it->second->info->key, key);
            auto indices = processEntries(it->second->entries, condition);

            auto entries = std::make_shared<Entries>();
            for (auto indexIt : indices)
            {
                entries->addEntry(it->second->entries->get(indexIt));
            }

            return entries;
        }

        return std::make_shared<Entries>();
    }
    size_t commit(h256, int64_t, const std::vector<TableData::Ptr>& datas) override
    {
        for (auto it : datas)
        {
            auto table = it->info->name;
            auto tableIt = tableData.find(table);
            if (tableIt != tableData.end())
            {
                size_t count = 0;
                auto countIt = tableCounter.find(table);
                if (countIt != tableCounter.end())
                {
                    count = countIt->second;
                }

                for (size_t i = 0; i < it->entries->size(); ++i)
                {
                    auto entry = it->entries->get(i);
                    if (entry->getID() == 0)
                    {
                        entry->setID(++count);
                        tableIt->second->entries->addEntry(entry);
                    }
                    else
                    {
                        for (size_t j = 0; j < tableIt->second->entries->size(); ++j)
                        {
                            if (tableIt->second->entries->get(j)->getID() == entry->getID())
                            {
                                for (auto fieldIt : *(entry->fields()))
                                {
                                    tableIt->second->entries->get(j)->setField(
                                        fieldIt.first, fieldIt.second);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        return datas.size();
    }
    bool onlyDirty() override { return false; }

private:
    tbb::concurrent_unordered_map<std::string, TableData::Ptr> tableData;
    tbb::concurrent_unordered_map<std::string, size_t> tableCounter;
};
}  // namespace storage

}  // namespace dev
