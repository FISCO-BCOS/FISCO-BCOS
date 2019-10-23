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

void RocksDBStorageFactory::setDBOpitons(rocksdb::Options _options)
{
    m_options = _options;
}

Storage::Ptr RocksDBStorageFactory::getStorage(const std::string& _dbName, bool _createIfMissing)
{
    auto dbName = m_DBPath + "/" + _dbName;
    if (!_createIfMissing && !boost::filesystem::exists(dbName))
    {
        return nullptr;
    }
    boost::filesystem::create_directories(dbName);

    RecursiveGuard l(x_cache);
    if (m_cache.first == dbName)
    {
        return m_cache.second;
    }
    std::shared_ptr<BasicRocksDB> rocksDB = std::make_shared<BasicRocksDB>();
    rocksDB->Open(m_options, dbName);
    if (m_encryptHandler && m_decryptHandler)
    {
        rocksDB->setEncryptHandler(m_encryptHandler);
        rocksDB->setDecryptHandler(m_decryptHandler);
    }
    std::shared_ptr<RocksDBStorage> rocksdbStorage =
        std::make_shared<RocksDBStorage>(m_disableWAL, m_completeDirty);
    rocksdbStorage->setDB(rocksDB);
    m_cache = make_pair(dbName, rocksdbStorage);
    return rocksdbStorage;
}
