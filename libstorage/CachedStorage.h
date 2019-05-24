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
#include <tbb/recursive_mutex.h>
#include <tbb/spin_mutex.h>
#include <tbb/spin_rw_mutex.h>
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
    Caches();
    virtual ~Caches(){};

    typedef tbb::spin_rw_mutex RWMutex;
    typedef tbb::spin_rw_mutex::scoped_lock RWScoped;

    virtual std::string key();
    virtual void setKey(const std::string& key);
    virtual Entries::Ptr entries();
    virtual Entries* entriesPtr();
    virtual void setEntries(Entries::Ptr entries);
    virtual int64_t num() const;
    virtual void setNum(int64_t num);

    virtual RWMutex* mutex();
    virtual bool empty();
    virtual void setEmpty(bool empty);

private:
    RWMutex m_mutex;


    bool m_empty = true;
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
    virtual std::pair<std::unordered_map<std::string, Caches::Ptr>::iterator, bool>
    addCache(const std::string& key, Caches::Ptr cache);
    virtual void removeCache(const std::string& key);

    virtual std::unordered_map<std::string, Caches::Ptr>* caches();

private:
    TableInfo::Ptr m_tableInfo;
    std::unordered_map<std::string, Caches::Ptr> m_caches;
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

    virtual std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped> > selectNoCondition(h256 hash,
        int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr);

    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

    void setBackend(Storage::Ptr backend);
    void init();

    int64_t syncNum();
    void setSyncNum(int64_t syncNum);

    void setMaxCapacity(int64_t maxCapacity);
    void setMaxForwardBlock(size_t maxForwardBlock);

    size_t ID();

private:
    void touchMRU(std::string table, std::string key, ssize_t capacity);
    std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped> > touchCache(
        TableInfo::Ptr tableInfo, std::string key, bool write = false);
    std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped> > touchCacheNoLock(
        TableInfo::Ptr tableInfo, std::string key, bool write = false);

    tbb::spin_mutex m_clearMutex;
    void checkAndClear();

    void updateCapacity(ssize_t capacity);
    std::string readableCapacity(size_t num);

    std::unordered_map<std::string, TableCaches::Ptr> m_caches;
    tbb::spin_mutex m_cachesMutex;

    tbb::spin_mutex m_touchMutex;

    boost::multi_index_container<std::pair<std::string, std::string>,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<
                boost::multi_index::identity<std::pair<std::string, std::string> > > > >
        m_mru;
    tbb::spin_mutex m_mruMutex;

    // boost::multi_index
    Storage::Ptr m_backend;
    size_t m_ID = 1;

    boost::atomic_int64_t m_syncNum;
    boost::atomic_int64_t m_commitNum;
    tbb::atomic<int64_t> m_capacity;

    size_t m_maxForwardBlock = 10;
    int64_t m_maxCapacity = 256 * 1024 * 1024;  // default 256MB for cache

    std::chrono::system_clock::time_point m_lastClear;
    dev::ThreadPool::Ptr m_taskThreadPool;

    // stat
    tbb::atomic<uint64_t> m_hitTimes = 0;
    tbb::atomic<uint64_t> m_queryTimes = 0;
};

}  // namespace storage

}  // namespace dev
