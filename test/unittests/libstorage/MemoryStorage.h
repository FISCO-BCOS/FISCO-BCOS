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
class MemoryStorage : public Storage
{
public:
    typedef std::shared_ptr<MemoryStorage> Ptr;

    virtual ~MemoryStorage(){};

    virtual Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        auto search = data.find(table);
        if (search != data.end())
        {
            auto tableData = search->second;
            auto it = tableData->data.find(key);
            if (it != tableData->data.end())
            {
                for (size_t i = 0; i < it->second->size(); ++i)
                {
                    if (it->second->get(i)->getStatus() == Entry::Status::DELETED)
                    {
                        it->second->removeEntry(i);
                    }
                }
                return it->second;
            }
        }
        return std::make_shared<Entries>();
    }
    virtual size_t commit(
        h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas, h256 blockHash) override
    {
        for (auto it : datas)
        {
            data[it->tableName] = it;
        }
        return datas.size();
    }
    virtual bool onlyDirty() override { return false; }

private:
    std::map<std::string, TableData::Ptr> data;
};
}  // namespace storage

}  // namespace dev
