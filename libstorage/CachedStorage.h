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
/** @file storage.h
 *  @author monan
 *  @date 20190409
 */

#pragma once

#include "Storage.h"
#include "Table.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/ThreadPool.h>
#include <tbb/mutex.h>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace dev
{
namespace storage
{
class Caches
{
public:
    typedef std::shared_ptr<Caches> Ptr;
    virtual ~Caches(){};

    virtual std::string key();
    virtual void setKey(const std::string& key);
    virtual Entries::Ptr entries();
    virtual void setEntries(Entries::Ptr entries);
    virtual int64_t num() const;
    virtual void setNum(int64_t num);

private:
    std::string m_key;
    Entries::Ptr m_entries;
    // int64_t m_num;
    tbb::atomic<int64_t> m_num;
};

class TableCaches
{
public:
    typedef std::shared_ptr<TableCaches> Ptr;
    TableCaches() { m_tableInfo = std::make_shared<TableInfo>(); }
    virtual ~TableCaches(){};

    virtual TableInfo::Ptr tableInfo();
    virtual void setTableInfo(TableInfo::Ptr tableInfo);
    virtual Caches::Ptr findCache(const std::string& key);
    virtual std::pair<tbb::concurrent_unordered_map<std::string, Caches::Ptr>::iterator, bool>
    addCache(const std::string& key, Caches::Ptr cache);
    virtual void removeCache(const std::string& key);

    virtual tbb::concurrent_unordered_map<std::string, Caches::Ptr>* caches();

private:
    TableInfo::Ptr m_tableInfo;
    tbb::concurrent_unordered_map<std::string, Caches::Ptr> m_caches;
    std::mutex m_mutex;
};

class Task
{
public:
    typedef std::shared_ptr<Task> Ptr;

    h256 hash;
    int64_t num = 0;
    std::shared_ptr<std::vector<TableData::Ptr> > datas;
};

class CachedStorage : public Storage
{
public:
    typedef std::shared_ptr<CachedStorage> Ptr;
    CachedStorage();

    virtual ~CachedStorage();

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;
    virtual Caches::Ptr selectNoCondition(h256 hash, int num, TableInfo::Ptr tableInfo,
        const std::string& key, Condition::Ptr condition = nullptr);
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

    void setBackend(Storage::Ptr backend);
    void init();

    int64_t syncNum();
    void setSyncNum(int64_t syncNum);

    void setMaxStoreKey(size_t maxStoreKey);
    void setMaxForwardBlock(size_t maxForwardBlock);

    size_t ID();

private:
    void touchMRU(std::string table, std::string key);
    void checkAndClear();

    tbb::concurrent_unordered_map<std::string, TableCaches::Ptr> m_caches;
    boost::multi_index_container<std::pair<std::string, std::string>,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<
                boost::multi_index::identity<std::pair<std::string, std::string> > > > >
        m_mru;
    // boost::multi_index
    Storage::Ptr m_backend;
    size_t m_ID = 1;

    boost::atomic_int64_t m_syncNum;
    boost::atomic_int64_t m_commitNum;
    boost::atomic_int64_t m_capacity;

    size_t m_maxStoreKey = 1000;
    size_t m_maxForwardBlock = 10;

    tbb::mutex m_mutex;

    dev::ThreadPool::Ptr m_taskThreadPool;

    // stat
    tbb::atomic<uint64_t> m_hitTimes = 0;
    tbb::atomic<uint64_t> m_queryTimes = 0;
};

}  // namespace storage

}  // namespace dev
