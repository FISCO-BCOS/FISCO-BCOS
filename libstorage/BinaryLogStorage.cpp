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

#include "BinaryLogStorage.h"
#include "BinLogHandler.h"
#include "StorageException.h"
#include <libdevcore/Common.h>

using namespace dev;
using namespace dev::storage;

BinaryLogStorage::BinaryLogStorage() {}

BinaryLogStorage::~BinaryLogStorage()
{
    stop();
}

Entries::Ptr BinaryLogStorage::select(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    if (m_backend)
    {
        // STORAGE_LOG(DEBUG) << LOG_DESC("BinLog select from backend") << LOG_KV("key", key);

        return m_backend->select(num, tableInfo, key, condition);
    }
    STORAGE_LOG(FATAL) << "No backend storage, go die!";
    BOOST_THROW_EXCEPTION(StorageException(-1, std::string("There is not a backend storage!")));
    return nullptr;
}

size_t BinaryLogStorage::commit(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    STORAGE_LOG(INFO) << "BinaryLogStorage commit: " << datas.size() << " num: " << num;

    if (m_binaryLogger)
    {
        if (!m_binaryLogger->writeBlocktoBinLog(num, datas))
        {
            STORAGE_LOG(FATAL) << LOG_DESC("BinLog writeBlocktoBinLog failed");
            BOOST_THROW_EXCEPTION(StorageException(-1, std::string("writeBlocktoBinLog failed!")));
        }
        STORAGE_LOG(DEBUG) << LOG_DESC("BinLog writeBlocktoBinLog successfully");
    }
    else
    {
        STORAGE_LOG(TRACE) << LOG_DESC("BinLog is off");
    }

    if (m_backend)
    {
        return m_backend->commit(num, datas);
    }
    STORAGE_LOG(FATAL) << "No backend storage, go die!";
    BOOST_THROW_EXCEPTION(StorageException(-1, std::string("There is not a backend storage!")));
    return -1;
}

void BinaryLogStorage::stop()
{
    STORAGE_LOG(INFO) << "Stop BinaryLogStorage";
}
