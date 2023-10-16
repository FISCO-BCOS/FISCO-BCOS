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
  //RocksDBStorageFactory 类继承自名为 StorageFactory 的基类，这暗示它用于创建和管理存储实例。
class RocksDBStorageFactory : public StorageFactory
{
  //构造函数接受数据库路径 _dbPath、是否禁用 Write-Ahead Logging（WAL） _disableWAL，以及是否启用 Complete Dirty _enableCompleteDirty 作为参数。
public:
//setDBOpitons 函数用于设置私有成员变量 m_options，存储 RocksDB 的选项配置
    RocksDBStorageFactory(const std::string& _dbPath, bool _disableWAL, bool _enableCompleteDirty)
      : m_DBPath(_dbPath), m_disableWAL(_disableWAL), m_completeDirty(_enableCompleteDirty)
    {}
    virtual ~RocksDBStorageFactory() {}
    void setDBOpitons(rocksdb::Options _options);
    //getStorage 函数用于获取存储实例。它接受数据库名称 _dbName 和一个布尔值 
    //_createIfMissing 作为参数，并返回一个 Storage::Ptr 对象。
    Storage::Ptr getStorage(const std::string& _dbName, bool _createIfMissing = true) override;
/*首先构建完整的数据库路径，并根据需要创建数据库文件夹。
使用 RecursiveGuard 进行线程保护，确保并发访问安全。
检查缓存中是否已存在 _dbName 对应的实例，如果存在则直接返回缓存中的实例。
否则，创建一个名为 rocksDB 的 BasicRocksDB 实例，并打开指定路径的数据库。
创建一个名为 rocksdbStorage 的 RocksDBStorage 实例，传递配置参数并设置数据库。
将数据库路径和 rocksdbStorage 实例存储到缓存中，以便下次使用。
返回 rocksdbStorage 实例作为函数的结果。*/
private:
    const std::string m_DBPath;//const std::string m_DBPath 存储数据库路径。
    bool m_disableWAL = false;//bool m_disableWAL 和 bool m_completeDirty 分别用于标识是否禁用 WAL 和是否启用 Complete Dirty。
    bool m_completeDirty = false;
    rocksdb::Options m_options;//rocksdb::Options m_options 用于存储 RocksDB 的选项配置。
    std::recursive_mutex x_cache;//std::recursive_mutex x_cache 是递归互斥锁，用于线程同步。
    std::pair<std::string, Storage::Ptr> m_cache;//std::pair<std::string, Storage::Ptr> m_cache 存储数据库路径和对应的存储实例。
};
}  // namespace storage
}  // namespace dev
