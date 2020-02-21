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
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
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
class Cache
{
public:
    typedef std::shared_ptr<Cache> Ptr;
    Cache();
    virtual ~Cache(){};

    typedef tbb::spin_rw_mutex RWMutex;
    typedef tbb::spin_rw_mutex::scoped_lock RWScoped;

    virtual std::string key();
    virtual void setKey(const std::string& key);
    virtual Entries::Ptr entries();
    virtual Entries* entriesPtr();
    virtual void setEntries(Entries::Ptr entries);
    virtual uint64_t num() const;
    virtual void setNum(int64_t num);
    virtual TableInfo::Ptr tableInfo();
    virtual void setTableInfo(TableInfo::Ptr tableInfo);

    virtual RWMutex* mutex();
    virtual bool empty();
    virtual void setEmpty(bool empty);

private:
    RWMutex m_mutex;

    TableInfo::Ptr m_tableInfo;

    bool m_empty = true;
    std::string m_key;
    Entries::Ptr m_entries;
    // int64_t m_num;
    tbb::atomic<uint64_t> m_num;
};

class Task
{
public:
    typedef std::shared_ptr<Task> Ptr;

    int64_t num = 0;
    std::shared_ptr<std::vector<TableData::Ptr> > datas;
};

class CachedStorage : public Storage
{
public:
    typedef std::shared_ptr<CachedStorage> Ptr;
    CachedStorage(dev::GROUP_ID const& _groupID = 0);

    typedef tbb::spin_rw_mutex RWMutex;
    typedef tbb::spin_rw_mutex::scoped_lock RWMutexScoped;

    typedef tbb::mutex Mutex;
    typedef tbb::mutex::scoped_lock MutexScoped;

    virtual ~CachedStorage();

    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;

    virtual std::tuple<std::shared_ptr<Cache::RWScoped>, Cache::Ptr> selectNoCondition(int64_t num,
        TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition = nullptr);

    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyCommitDirty() override { return true; }

    void setBackend(Storage::Ptr backend);
    void init();
    void stop() override;

    void clear();

    int64_t syncNum();
    void setSyncNum(int64_t syncNum);
    void setClearInterval(size_t _clearIntervalSecond)
    {
        m_clearInterval = _clearIntervalSecond * 1000;
    }
    void setMaxCapacity(int64_t maxCapacity);
    void setMaxForwardBlock(size_t maxForwardBlock);

    void startClearThread();
    dev::GROUP_ID groupID() const { return m_groupID; }

protected:
    dev::GROUP_ID m_groupID = 0;

private:
    void touchMRU(const std::string& table, const std::string& key, ssize_t capacity);
    void updateMRU(const std::string& table, const std::string& key, ssize_t capacity);
    std::tuple<std::shared_ptr<Cache::RWScoped>, Cache::Ptr, bool> touchCache(
        TableInfo::Ptr table, const std::string& key, bool write = false);
    void restoreCache(TableInfo::Ptr table, const std::string& key, Cache::Ptr cache);

    void removeCache(const std::string& table, const std::string& key);

    bool disabled();

    bool commitBackend(Task::Ptr task);

    void checkAndClear();

    void updateCapacity(ssize_t capacity);
    std::string readableCapacity(size_t num);

    tbb::concurrent_unordered_map<std::string, Cache::Ptr> m_caches;
    RWMutex m_cachesMutex;

    Mutex m_commitMutex;

    std::shared_ptr<boost::multi_index_container<std::pair<std::string, std::string>,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<
                boost::multi_index::identity<std::pair<std::string, std::string> > > > > >
        m_mru;
    std::shared_ptr<tbb::concurrent_queue<std::tuple<std::string, std::string, ssize_t> > >
        m_mruQueue;

    // boost::multi_index
    Storage::Ptr m_backend;


    tbb::atomic<uint64_t> m_syncNum;
    tbb::atomic<uint64_t> m_commitNum;
    tbb::atomic<int64_t> m_capacity;

    // config
    uint64_t m_maxForwardBlock = 10;
    int64_t m_maxCapacity = 256 * 1024 * 1024;  // default 256MB for cache
    uint64_t m_clearInterval = 1000;

    dev::ThreadPool::Ptr m_taskThreadPool;
    std::shared_ptr<std::thread> m_clearThread;

    tbb::atomic<uint64_t> m_hitTimes;
    tbb::atomic<uint64_t> m_queryTimes;

    std::shared_ptr<tbb::atomic<bool> > m_committing;
    std::shared_ptr<tbb::atomic<bool> > m_running;
};

}  // namespace storage

}  // namespace dev
