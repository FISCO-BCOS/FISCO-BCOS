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
/**
 * @brief : Encrypt level DB
 * @author: jimmyshi, websterchen
 * @date: 2018-11-26
 */

#pragma once
#include "Common.h"
#include <leveldb/db.h>
#include <leveldb/slice.h>
#include <libdevcore/BasicLevelDB.h>
#include <string>

namespace dev
{
namespace db
{
#define ENCDB_LOG(_OBV) LOG(_OBV) << "[g:" << m_name << "][ENCDB]"

class EncryptedLevelDBWriteBatch : public LevelDBWriteBatch
{
public:
    EncryptedLevelDBWriteBatch(const dev::bytes& _dataKey, const std::string& _name = "")
      : m_dataKey(_dataKey), m_name(_name)
    {}
    void insertSlice(const leveldb::Slice& _key, const leveldb::Slice& _value) override;

private:
    dev::bytes m_dataKey;
    std::string m_name;
};

class EncryptedLevelDB : public BasicLevelDB
{
public:
    EncryptedLevelDB(const leveldb::Options& _options, const std::string& _name,
        const std::string& _cipherDataKey, const std::string& _dataKey);
    ~EncryptedLevelDB(){};

    static leveldb::Status Open(const leveldb::Options& _options, const std::string& _name,
        BasicLevelDB** _dbptr, const std::string& _cipherDataKey,
        const std::string& _dataKey);  // DB open
    leveldb::Status Get(const leveldb::ReadOptions& _options, const leveldb::Slice& _key,
        std::string* _value) override;
    leveldb::Status Put(const leveldb::WriteOptions& _options, const leveldb::Slice& _key,
        const leveldb::Slice& _value) override;

    std::unique_ptr<LevelDBWriteBatch> createWriteBatch() const override;

    enum class OpenDBStatus
    {
        FirstCreation = 0,
        NoEncrypted,
        CipherKeyError,
        Encrypted
    };

private:
    std::string m_cipherDataKey;
    dev::bytes m_dataKey;

private:
    std::string getKeyOfDatabase();
    void setCipherDataKey(std::string _cipherDataKey);
    EncryptedLevelDB::OpenDBStatus checkOpenDBStatus();
};
}  // namespace db
}  // namespace dev