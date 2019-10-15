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
 * (c) 2016-2020 fisco-dev contributors.
 */
/** @file BinaryLogStorage.h
 *  @author xingqiangbai
 *  @date 20190823
 */

#pragma once

#include "Storage.h"

namespace dev
{
namespace storage
{
class BinLogHandler;
class BinaryLogStorage : public Storage
{
public:
    typedef std::shared_ptr<BinaryLogStorage> Ptr;
    BinaryLogStorage();

    virtual ~BinaryLogStorage();

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;

    void setBackend(Storage::Ptr backend) { m_backend = backend; }
    virtual void setBinaryLogger(std::shared_ptr<BinLogHandler> _logger)
    {
        m_binaryLogger = _logger;
    }
    void stop() override;

private:
    Storage::Ptr m_backend;
    std::shared_ptr<BinLogHandler> m_binaryLogger = nullptr;
};

}  // namespace storage

}  // namespace dev
