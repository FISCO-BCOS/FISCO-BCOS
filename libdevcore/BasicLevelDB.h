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
 * @brief : basic level DB
 * @author: jimmyshi
 * @date: 2018-11-26
 */

#pragma once

#include "Guards.h"
#include "db.h"
#include <leveldb/db.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>
#include <memory>
#include <string>

namespace dev
{
namespace db
{
class LevelDBWriteBatch : public WriteBatchFace
{
public:
    void insert(Slice _key, Slice _value) override;
    void kill(Slice _key) override;
    // void append(const LevelDBWriteBatch& _batch);
    const leveldb::WriteBatch& writeBatch() const { return m_writeBatch; }
    leveldb::WriteBatch& writeBatch() { return m_writeBatch; }

    // For Encrypted level DB
    virtual void insertSlice(const leveldb::Slice& _key, const leveldb::Slice& _value);

protected:
    leveldb::WriteBatch m_writeBatch;
    dev::SharedMutex x_writeBatch;
};

class BasicLevelDB
{
public:
    BasicLevelDB() {}
    BasicLevelDB(const leveldb::Options& _options, const std::string& _name);
    virtual ~BasicLevelDB(){};

    static leveldb::Status Open(
        const leveldb::Options& _options, const std::string& _name, BasicLevelDB** _dbptr);

    virtual leveldb::Status Write(
        const leveldb::WriteOptions& _options, leveldb::WriteBatch* _updates);

    virtual leveldb::Status Get(
        const leveldb::ReadOptions& _options, const leveldb::Slice& _key, std::string* _value);

    virtual leveldb::Status Put(const leveldb::WriteOptions& _options, const leveldb::Slice& _key,
        const leveldb::Slice& _value);

    virtual leveldb::Status Delete(
        const leveldb::WriteOptions& _options, const leveldb::Slice& _key);

    virtual leveldb::Iterator* NewIterator(const leveldb::ReadOptions& _options);

    virtual std::unique_ptr<LevelDBWriteBatch> createWriteBatch() const;

    leveldb::Status OpenStatus() { return m_openStatus; }

    bool empty();

protected:
    std::shared_ptr<leveldb::DB> m_db;
    leveldb::Status m_openStatus;
    std::string m_name;
};
}  // namespace db
}  // namespace dev
