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
/** @file RocksDBStorageFactory.h
 *  @author xingqiangbai
 *  @date 20180423
 */
#pragma once

#include "Storage.h"
#include "rocksdb/options.h"

namespace rocksdb
{
class DB;
}
namespace dev
{
namespace storage
{
class RocksDBStorageFactory : public StorageFactory
{
public:
    RocksDBStorageFactory(const std::string& _dbPath) : m_DBPath(_dbPath) {}
    virtual ~RocksDBStorageFactory() {}
    void setDBOpitons(rocksdb::Options _options) { m_options = _options; }
    Storage::Ptr getStorage(const std::string& _dbName) override;

private:
    bool m_disableWAL = false;
    const std::string m_DBPath;
    rocksdb::Options m_options;
    std::recursive_mutex x_cache;
    std::pair<std::string, Storage::Ptr> m_cache;
    std::function<void(std::string const&, std::string&)> m_encryptHandler = nullptr;
    std::function<void(std::string&)> m_decryptHandler = nullptr;
};
}  // namespace storage
}  // namespace dev
