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

#include "RocksDBStorageFactory.h"
#include "BasicRocksDB.h"
#include "RocksDBStorage.h"
#include "libdevcore/Guards.h"
#include <boost/filesystem.hpp>

using namespace std;
using namespace dev;
using namespace dev::storage;

//setDBOpitons 函数用于设置私有成员变量 m_options，该变量存储 RocksDB 的选项配置。
void RocksDBStorageFactory::setDBOpitons(rocksdb::Options _options)
{
    m_options = _options;
}
//getStorage 函数用于获取 RocksDBStorage 实例。它接受数据库名称 _dbName 和一个布尔值 _createIfMissing 作为参数。
Storage::Ptr RocksDBStorageFactory::getStorage(const std::string& _dbName, bool _createIfMissing)
{   //首先，根据数据库名称构建完整路径，并检查是否需要创建数据库（如果不存在）。如果不需要创建且数据库不存在，函数返回nullptr，表示未找到该数据库
    auto dbName = m_DBPath + "/" + _dbName;
    if (!_createIfMissing && !boost::filesystem::exists(dbName))
    {
        return nullptr;
    }
    boost::filesystem::create_directories(dbName);
    //然后，使用 boost::filesystem::create_directories 创建数据库文件夹。在创建数据库实例之前，使用 RecursiveGuard 进行线程保护，防止并发访问
    RecursiveGuard l(x_cache);
    if (m_cache.first == dbName)
    {
        return m_cache.second;
    }
    //如果缓存中已存在该数据库实例，则直接返回缓存中的实例，避免重复创建。否则，创建一个名为 rocksDB 的 BasicRocksDB 实例，并打开指定路径的数据库。
    std::shared_ptr<BasicRocksDB> rocksDB = std::make_shared<BasicRocksDB>();
    //接着，创建一个名为 rocksdbStorage 的 RocksDBStorage 实例，传递配置参数并设置数据库。
    rocksDB->Open(m_options, dbName);
    std::shared_ptr<RocksDBStorage> rocksdbStorage =
        std::make_shared<RocksDBStorage>(m_disableWAL, m_completeDirty);
        //将数据库路径和 rocksdbStorage 实例存储到缓存中，以便下次使用
    rocksdbStorage->setDB(rocksDB);
    m_cache = make_pair(dbName, rocksdbStorage);
    return rocksdbStorage;
    //最后，返回 rocksdbStorage 实例作为函数的结果。
}
