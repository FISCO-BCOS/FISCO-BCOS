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
#include <tbb/mutex.h>

namespace dev
{
namespace storage
{
class MemoryStorage2 : public Storage
{
public:
    typedef std::shared_ptr<MemoryStorage2> Ptr;

    virtual ~MemoryStorage2(){};

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        (void)num;

        tbb::mutex::scoped_lock lock(m_mutex);

        auto it = tableData.find(tableInfo->name);

        if (it != tableData.end())
        {
            return it->second;
        }

        return std::make_shared<Entries>();
    }
    size_t commit(int64_t, const std::vector<TableData::Ptr>& datas) override
    {
        for (auto it : datas)
        {
        }
        return datas.size();
    }
    bool onlyCommitDirty() override { return false; }

private:
    std::map<std::string, TableData::Ptr> tableData;
    std::map<std::string, Entries::Ptr> key2Entries;
    tbb::mutex m_mutex;
};
}  // namespace storage

}  // namespace dev
