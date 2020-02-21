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

#include "CachedStorage.h"
#include "StorageException.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::storage;

Cache::Cache()
{
    m_entries = std::make_shared<Entries>();
    m_num.store(0);
}

std::string Cache::key()
{
    return m_key;
}

void Cache::setKey(const std::string& key)
{
    m_key = key;
}

Entries::Ptr Cache::entries()
{
    return m_entries;
}

Entries* Cache::entriesPtr()
{
    return m_entries.get();
}

void Cache::setEntries(Entries::Ptr entries)
{
    m_entries = entries;
}

uint64_t Cache::num() const
{
    return m_num;
}

void Cache::setNum(int64_t num)
{
    m_num = num;
}

Cache::RWMutex* Cache::mutex()
{
    return &m_mutex;
}

bool Cache::empty()
{
    return m_empty;
}

void Cache::setEmpty(bool empty)
{
    m_empty = empty;
}

TableInfo::Ptr Cache::tableInfo()
{
    return m_tableInfo;
}

void Cache::setTableInfo(TableInfo::Ptr tableInfo)
{
    m_tableInfo = tableInfo;
}

CachedStorage::CachedStorage(dev::GROUP_ID const& _groupID) : m_groupID(_groupID)
{
    CACHED_STORAGE_LOG(INFO) << "Init flushStorage thread";
    m_taskThreadPool =
        std::make_shared<dev::ThreadPool>("taskPool-" + std::to_string(m_groupID), 1);

    m_mruQueue =
        std::make_shared<tbb::concurrent_queue<std::tuple<std::string, std::string, ssize_t>>>();
    m_mru = std::make_shared<boost::multi_index_container<std::pair<std::string, std::string>,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<
                boost::multi_index::identity<std::pair<std::string, std::string>>>>>>();
    m_syncNum.store(0);
    m_commitNum.store(0);
    m_capacity.store(0);

    m_hitTimes.store(0);
    m_queryTimes.store(0);

    m_running = std::make_shared<tbb::atomic<bool>>();
    m_running->store(true);
    m_committing = std::make_shared<tbb::atomic<bool>>();
    m_committing->store(false);
}

CachedStorage::~CachedStorage()
{
    if (m_running->load())
    {
        stop();
    }
}

Entries::Ptr CachedStorage::select(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    auto out = std::make_shared<Entries>();

    auto result = selectNoCondition(num, tableInfo, key, condition);

    Cache::Ptr caches = std::get<1>(result);
    for (auto entry : *(caches->entries()))
    {
        if (condition && !condition->process(entry))
        {
            continue;
        }
        auto outEntry = std::make_shared<Entry>();
        outEntry->copyFrom(entry);
        out->addEntry(outEntry);
    }

    return out;
}

std::tuple<std::shared_ptr<Cache::RWScoped>, Cache::Ptr> CachedStorage::selectNoCondition(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    (void)condition;

    if (!tableInfo->enableCache)
    {
        std::shared_ptr<RWMutexScoped> emptyScoped;
        auto cache = std::make_shared<Cache>();

        if (m_backend)
        {
            auto conditionKey = std::make_shared<Condition>();
            conditionKey->EQ(tableInfo->key, key);
            auto backendData = m_backend->select(num, tableInfo, key, conditionKey);

            cache->setEntries(backendData);
            cache->setEmpty(false);
        }
        else
        {
            CACHED_STORAGE_LOG(FATAL) << "CachedStorage needs a backend storage.";
        }

        return std::make_tuple(emptyScoped, cache);
    }

    auto result = touchCache(tableInfo, key, true);
    auto caches = std::get<1>(result);

    if (caches->empty())
    {
        if (m_backend)
        {
            auto conditionKey = std::make_shared<Condition>();
            conditionKey->EQ(tableInfo->key, key);
            auto backendData = m_backend->select(num, tableInfo, key, conditionKey);

            CACHED_STORAGE_LOG(TRACE) << tableInfo->name << ": " << key << " miss the cache";

            caches->setEntries(backendData);
            caches->setEmpty(false);

            size_t totalCapacity = 0;
            for (auto it : *backendData)
            {
                totalCapacity += it->capacity();
            }
            touchMRU(tableInfo->name, key, totalCapacity);
        }
        else
        {
            CACHED_STORAGE_LOG(FATAL) << "CachedStorage needs a backend storage.";
        }
    }
    else
    {
        touchMRU(tableInfo->name, key, 0);
    }

    return std::make_tuple(std::get<0>(result), caches);
}

size_t CachedStorage::commit(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    CACHED_STORAGE_LOG(INFO) << "CachedStorage commit: " << datas.size() << " num: " << num;

    tbb::atomic<size_t> total = 0;

    TIME_RECORD("Process dirty entries");
    std::shared_ptr<std::vector<TableData::Ptr>> commitDatas =
        std::make_shared<std::vector<TableData::Ptr>>();

    m_committing->store(true);
    commitDatas->resize(datas.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, datas.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx)
            {
                auto requestData = datas[idx];

                auto commitData = std::make_shared<TableData>();
                commitData->info = requestData->info;

                if (!commitData->info->enableCache)
                {
                    commitData->dirtyEntries->shallowFrom(requestData->dirtyEntries);
                }
                else
                {
                    commitData->dirtyEntries->resize(requestData->dirtyEntries->size());

                    // addtion data
                    std::set<std::string> addtionKey;
                    std::set<uint64_t> duplicateIDs;
                    tbb::spin_mutex addtionKeyMutex;

                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, requestData->dirtyEntries->size()),
                        [&](const tbb::blocked_range<size_t>& rangeEntries) {
                            for (size_t i = rangeEntries.begin(); i < rangeEntries.end(); ++i)
                            {
                                ++total;

                                auto entry = requestData->dirtyEntries->get(i);
                                auto key = entry->getField(requestData->info->key);
                                auto id = entry->getID();

                                ssize_t change = 0;
                                if (id == 0)
                                {
                                    // impossible, should exit
                                    CACHED_STORAGE_LOG(FATAL)
                                        << "Dirty entry id equal to 0, table: "
                                        << requestData->info->name << LOG_KV("key", key);
                                }
                                else
                                {
                                    auto result = touchCache(requestData->info, key, true);
                                    auto caches = std::get<1>(result);
                                    if (caches->empty())
                                    {
                                        if (m_backend)
                                        {
                                            auto conditionKey = std::make_shared<Condition>();
                                            conditionKey->EQ(requestData->info->key, key);
                                            auto backendData = m_backend->select(
                                                num, requestData->info, key, conditionKey);

                                            CACHED_STORAGE_LOG(DEBUG)
                                                << requestData->info->name << "-" << key
                                                << " miss the cache while commit dirty entries";

                                            caches->setEntries(backendData);

                                            size_t totalCapacity = 0;
                                            for (auto it : *backendData)
                                            {
                                                totalCapacity += it->capacity();
                                            }


                                            touchMRU(requestData->info->name, key, totalCapacity);
                                        }

                                        restoreCache(requestData->info, key, caches);
                                    }

                                    caches->setNum(num);
                                    caches->setEmpty(false);

                                    auto entryIt = std::lower_bound(caches->entries()->begin(),
                                        caches->entries()->end(), entry,
                                        [](const Entry::Ptr& lhs, const Entry::Ptr& rhs) {
                                            return lhs->getID() < rhs->getID();
                                        });

                                    if (entryIt != caches->entries()->end() &&
                                        (*entryIt)->getID() == id)
                                    {
                                        auto oldSize = (*entryIt)->capacity();

                                        for (auto fieldIt : *entry)
                                        {
                                            (*entryIt)->setField(fieldIt.first, fieldIt.second);
                                        }
                                        (*entryIt)->setStatus(entry->getStatus());

                                        change = (ssize_t)(
                                            (ssize_t)(*entryIt)->capacity() - (ssize_t)oldSize);

                                        (*entryIt)->setNum(num);

                                        auto commitEntry = std::make_shared<Entry>();
                                        commitEntry->copyFrom(*entryIt);
                                        (*commitData->dirtyEntries)[i] = commitEntry;
                                        if (m_backend && !m_backend->onlyCommitDirty())
                                        {  // Only for RocksDB
                                            tbb::spin_mutex::scoped_lock lock(addtionKeyMutex);
                                            auto inserted = addtionKey.insert(key).second;

                                            if (inserted)
                                            {
                                                for (auto it = caches->entries()->begin();
                                                     it != caches->entries()->end(); ++it)
                                                {
                                                    if (it != entryIt)
                                                    {  // This addEntry is necessary, because
                                                        // backend storage processDirtyEntries()
                                                        // will not get data from real DB
                                                        auto copyLostEntry = make_shared<Entry>();
                                                        copyLostEntry->copyFrom(*it);
                                                        commitData->dirtyEntries->addEntry(
                                                            copyLostEntry);
                                                    }
                                                }
                                            }
                                            else
                                            {
                                                CACHED_STORAGE_LOG(DEBUG)
                                                    << LOG_BADGE("duplicate entry")
                                                    << LOG_KV("key", key)
                                                    << LOG_KV("ID", (*entryIt)->getID());
                                                duplicateIDs.insert((*entryIt)->getID());
                                            }
                                        }
                                    }
                                    else
                                    {
                                        // impossible, should exit
                                        CACHED_STORAGE_LOG(FATAL)
                                            << "Can not find entry in cache, key:" << key
                                            << LOG_KV("num", num) << ",id" << id
                                            << " != " << (*entryIt)->getID();
                                    }
                                }

                                touchMRU(requestData->info->name, key, change);
                            }
                        });
                    tbb::parallel_for(tbb::blocked_range<size_t>(requestData->dirtyEntries->size(),
                                          commitData->dirtyEntries->size()),
                        [&](const tbb::blocked_range<size_t>& rangeEntries) {
                            for (size_t i = rangeEntries.begin(); i < rangeEntries.end(); ++i)
                            {  // remove duplicate entries
                                if (duplicateIDs.find((*commitData->dirtyEntries)[i]->getID()) !=
                                    duplicateIDs.end())
                                {
                                    auto duplicateEntry = make_shared<Entry>();
                                    duplicateEntry->copyFrom((*commitData->dirtyEntries)[i]);
                                    duplicateEntry->setDeleted(true);
                                    (*commitData->dirtyEntries)[i] = duplicateEntry;
                                }
                            }
                        });
                    tbb::parallel_sort(commitData->dirtyEntries->begin(),
                        commitData->dirtyEntries->end(), EntryLessNoLock(commitData->info));
                }

                commitData->newEntries->shallowFrom(requestData->newEntries);
                (*commitDatas)[idx] = commitData;
            }
        });

    TIME_RECORD("Process new entries");
    auto commitDatasSize = commitDatas->size();
    for (size_t i = 0; i < commitDatasSize; ++i)
    {
        auto commitData = (*commitDatas)[i];

        if (!commitData->info->enableCache)
        {
            continue;
        }

        auto newEntriesSize = commitData->newEntries->size();
        for (size_t j = 0; j < newEntriesSize; ++j)
        {
            auto commitEntry = commitData->newEntries->get(j);
            commitEntry->setNum(num);
            ++total;

            auto key = commitEntry->getField(commitData->info->key);

            auto cacheEntry = std::make_shared<Entry>();
            cacheEntry->copyFrom(commitEntry);

            if (cacheEntry->force())
            {
                auto result = touchCache(commitData->info, key, true);

                auto caches = std::get<1>(result);
                caches->setNum(num);
                caches->entries()->addEntry(cacheEntry);
                caches->setEmpty(false);
            }
            else
            {
                auto result = touchCache(commitData->info, key, true);
                auto caches = std::get<1>(result);
                if (caches->empty())
                {
                    if (m_backend)
                    {
                        auto conditionKey = std::make_shared<Condition>();
                        conditionKey->EQ(commitData->info->key, key);
                        auto backendData =
                            m_backend->select(num, commitData->info, key, conditionKey);

                        CACHED_STORAGE_LOG(TRACE) << commitData->info->name << "-" << key
                                                  << " miss the cache while commit new entries";

                        caches->setEntries(backendData);

                        size_t totalCapacity = 0;
                        for (auto it : *backendData)
                        {
                            totalCapacity += it->capacity();
                        }
#if 0
                        CACHED_STORAGE_LOG(TRACE) << "backend capacity: " << commitData->info->name
                                                  << "-" << key << ", capacity: " << totalCapacity;
#endif
                        touchMRU(commitData->info->name, key, totalCapacity);
                    }

                    restoreCache(commitData->info, key, caches);
                }

                caches->entries()->addEntry(cacheEntry);
                caches->setNum(num);
                caches->setEmpty(false);
            }
#if 0
            STORAGE_LOG(TRACE) << "new cached: " << commitData->info->name << "-" << key
                               << ", capacity: " << cacheEntry->capacity();
#endif
            touchMRU(commitData->info->name, key, cacheEntry->capacity());
        }
    }
    m_committing->store(false);

    if (m_backend)
    {
        TIME_RECORD("Submit commit task");
        // new task write to backend
        Task::Ptr task = std::make_shared<Task>();
        task->num = num;
        task->datas = commitDatas;

        auto backend = m_backend;
        auto self = std::weak_ptr<CachedStorage>(
            std::dynamic_pointer_cast<CachedStorage>(shared_from_this()));

        m_commitNum.store(num);

        if (!disabled())
        {
            m_taskThreadPool->enqueue([task, self]() {
                auto storage = self.lock();
                if (storage)
                {
                    storage->commitBackend(task);
                }
            });

            STORAGE_LOG(INFO) << "Submited block task: " << num
                              << ", current syncd block: " << m_syncNum;

            uint64_t waitCount = 0;
            while (((size_t)(m_commitNum - m_syncNum) > m_maxForwardBlock) && m_running->load())
            {
                CACHED_STORAGE_LOG(INFO)
                    << "Current block number: " << m_commitNum
                    << " greater than syncd block number: " << m_syncNum << ", waiting...";

                if (waitCount < 5)
                {
                    std::this_thread::yield();
                }
                else
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds((waitCount < 100 ? waitCount : 100) * 50));
                }

                ++waitCount;
            }
        }
        else
        {
            if (!commitBackend(task))
            {
                m_running->store(false);
                m_taskThreadPool->stop();
                raise(SIGTERM);
                BOOST_THROW_EXCEPTION(StorageException(-1, std::string("backend DB dead!")));
            }
        }
    }
    else
    {
        STORAGE_LOG(INFO) << "No backend storage, skip commit...";

        setSyncNum(num);
    }
    return total;
}

void CachedStorage::setBackend(Storage::Ptr backend)
{
    m_backend = backend;
}

void CachedStorage::init()
{
    if (!disabled())
    {
        startClearThread();
    }
}

void CachedStorage::stop()
{
    if (!m_running)
    {
        STORAGE_LOG(INFO) << LOG_DESC("CachedStorage already stopped!");
        return;
    }
    STORAGE_LOG(INFO) << "Stopping flushStorage thread";
    m_running->store(false);
    m_taskThreadPool->stop();

    if (m_clearThread)
    {
        if (m_clearThread->get_id() != std::this_thread::get_id())
        {
            m_clearThread->join();
            m_clearThread.reset();
        }
        else
        {
            m_clearThread->detach();
        }
    }
    STORAGE_LOG(INFO) << "flushStorage thread stopped.";
}

void CachedStorage::clear()
{
    RWMutexScoped lockCache(m_cachesMutex, true);

    m_caches.clear();
}

int64_t CachedStorage::syncNum()
{
    return m_syncNum;
}

void CachedStorage::setSyncNum(int64_t syncNum)
{
    m_syncNum.store(syncNum);
}

void CachedStorage::setMaxCapacity(int64_t maxCapacity)
{
    m_maxCapacity = maxCapacity;
}

void CachedStorage::setMaxForwardBlock(size_t maxForwardBlock)
{
    m_maxForwardBlock = maxForwardBlock;
}

void CachedStorage::startClearThread()
{
    std::weak_ptr<CachedStorage> self(std::dynamic_pointer_cast<CachedStorage>(shared_from_this()));
    auto running = m_running;
    m_clearThread = std::make_shared<std::thread>([running, self]() {
        while (true)
        {
            auto storage = self.lock();
            if (storage && storage->m_running->load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(storage->m_clearInterval));
                storage->checkAndClear();
            }
            else
            {
                return;
            }
        }
    });
}

void CachedStorage::touchMRU(const std::string& table, const std::string& key, ssize_t capacity)
{
    if (disabled())
    {
        return;
    }

    m_mruQueue->push(std::make_tuple(table, key, capacity));
}

void CachedStorage::updateMRU(const std::string& table, const std::string& key, ssize_t capacity)
{
    if (capacity != 0)
    {
        updateCapacity(capacity);
    }

    auto r = m_mru->push_back(std::make_pair(table, key));
    if (!r.second)
    {
        m_mru->relocate(m_mru->end(), r.first);
    }
}

std::tuple<std::shared_ptr<Cache::RWScoped>, Cache::Ptr, bool> CachedStorage::touchCache(
    TableInfo::Ptr tableInfo, const std::string& key, bool write)
{
    bool hit = true;

    ++m_queryTimes;

    auto cache = std::make_shared<Cache>();
    auto cacheKey = tableInfo->name + "_" + key;

    bool inserted = false;
    {
        RWMutexScoped lockCache(m_cachesMutex, false);

        auto result = m_caches.insert(std::make_pair(cacheKey, cache));

        cache = result.first->second;
        inserted = result.second;
    }

    auto cacheLock = std::make_shared<Cache::RWScoped>(*(cache->mutex()), write);
    if (inserted)
    {
        hit = false;

        cache->setKey(key);
        cache->setTableInfo(tableInfo);
    }

    if (hit)
    {
        ++m_hitTimes;
    }

    return std::make_tuple(cacheLock, cache, true);
}

void CachedStorage::restoreCache(TableInfo::Ptr table, const std::string& key, Cache::Ptr cache)
{
    /*
     If the checkAndClear() run ahead of commit() at same key, commit() may flush data to the
     cache object which erased in m_caches, the data will lost, to avoid this, re-insert the
     data into the m_caches
     */

    RWMutexScoped lockCache(m_cachesMutex, false);

    auto cacheKey = table->name + "_" + key;
    auto result = m_caches.insert(std::make_pair(cacheKey, cache));
    if (!result.second && result.first->second != cache)
    {
        CACHED_STORAGE_LOG(FATAL) << "Restore cache fail! Cache not equal: " << cacheKey << " "
                                  << result.first->second << " " << cache;

        exit(1);
    }
}

void CachedStorage::removeCache(const std::string& table, const std::string& key)
{
    auto cacheKey = table + "_" + key;
    RWMutexScoped lockCache(m_cachesMutex, true);

    auto c = m_caches.unsafe_erase(cacheKey);

    if (c != 1)
    {
        CACHED_STORAGE_LOG(FATAL) << "Can not remove cache: " << table << "-" << key;

        exit(1);
    }
}

bool CachedStorage::disabled()
{
    return ((m_maxCapacity == 0) && (m_maxForwardBlock == 0));
}

bool CachedStorage::commitBackend(Task::Ptr task)
{
    auto now = std::chrono::steady_clock::now();

    STORAGE_LOG(INFO) << "Start commit block: " << task->num << " to backend storage";
    try
    {
        m_backend->commit(task->num, *(task->datas));

        setSyncNum(task->num);

        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - now;
        STORAGE_LOG(INFO)
            << "[g:" << std::to_string(groupID()) << "]"
            << "\n---------------------------------------------------------------------\n"
            << "Commit block: " << task->num
            << " to backend storage finished, current cached block: " << m_commitNum << "\n"
            << "Flush elapsed time: " << std::setiosflags(std::ios::fixed) << std::setprecision(4)
            << elapsed.count() << "s"
            << "\n---------------------------------------------------------------------\n";

        if (disabled())
        {
            clear();
        }
    }
    catch (std::exception& e)
    {
        // stop() commit thread to exit
        m_running->store(false);
        m_taskThreadPool->stop();
        raise(SIGTERM);
        STORAGE_LOG(ERROR) << "Stop commit thread. Fail to commit data: " << e.what();
        return false;
    }
    return true;
}

void CachedStorage::checkAndClear()
{
    if (m_committing->load())
    {  // commit is inprocessing, dont clear
        return;
    }
    uint64_t count = 0;
    // calculate and calculate m_capacity with all elements of m_mruQueue
    // since inner loop will break once m_mruQueue is empty, here use while(true)
    while (true)
    {
        std::tuple<std::string, std::string, ssize_t> mru;
        auto result = m_mruQueue->try_pop(mru);
        if (!result)
        {
            break;
        }
        updateMRU(std::get<0>(mru), std::get<1>(mru), std::get<2>(mru));
        ++count;
    }

    CACHED_STORAGE_LOG(DEBUG) << "CheckAndClear pop: " << count << " elements";

    TIME_RECORD("Check and clear");

    bool needClear = false;
    size_t clearTimes = 0;

    auto currentCapacity = m_capacity.load();

    size_t clearCount = 0;
    size_t clearThrough = 0;
    do
    {
        needClear = false;
        if (m_syncNum > 0)
        {
            if (m_capacity > m_maxCapacity && !m_mru->empty() && !m_committing->load())
            {
                needClear = true;
            }
        }

        if (needClear)
        {
            for (auto it = m_mru->begin(); it != m_mru->end();)
            {
                if (m_capacity <= (int64_t)m_maxCapacity || m_mru->empty() || m_committing->load())
                {
                    break;
                }

                ++clearThrough;

                auto tableInfo = std::make_shared<TableInfo>();
                tableInfo->name = it->first;

                auto result = touchCache(tableInfo, it->second, true);
                auto cache = std::get<1>(result);

                if (std::get<2>(result))
                {
                    if (m_syncNum > 0 && (cache->num() <= m_syncNum))
                    {
                        int64_t totalCapacity = 0;
                        for (auto entryIt : *(cache->entries()))
                        {
                            totalCapacity += entryIt->capacity();
                        }

                        ++clearCount;
                        updateCapacity(0 - totalCapacity);

                        cache->setEmpty(true);
                        removeCache(it->first, it->second);
                        it = m_mru->erase(it);
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    ++it;
                }
            }
            ++clearTimes;
        }
    } while (needClear);

    if (clearThrough > 0)
    {
        CACHED_STORAGE_LOG(INFO) << "Clear finished, total: " << clearCount << " entries, "
                                 << "through: " << clearThrough << " entries, "
                                 << readableCapacity(currentCapacity - m_capacity)
                                 << ", Current total entries: " << m_caches.size()
                                 << ", Current total mru entries: " << m_mru->size()
                                 << ", total capacaity: " << readableCapacity(m_capacity);

        CACHED_STORAGE_LOG(DEBUG)
            << "Cache Status: \n\n"
            << "\n---------------------------------------------------------------------\n"
            << "Total query: " << m_queryTimes << "\n"
            << "Total cache hit: " << m_hitTimes << "\n"
            << "Total cache miss: " << m_queryTimes - m_hitTimes << "\n"
            << "Total hit ratio: " << std::setiosflags(std::ios::fixed) << std::setprecision(4)
            << ((double)m_hitTimes / m_queryTimes) * 100 << "%"
            << "\n\n"
            << "Cache capacity: " << readableCapacity(m_capacity) << "\n"
            << "Cache size: " << m_mru->size()
            << "\n---------------------------------------------------------------------\n";
    }
}

void CachedStorage::updateCapacity(ssize_t capacity)
{
    m_capacity.fetch_and_add(capacity);
}

std::string CachedStorage::readableCapacity(size_t num)
{
    std::stringstream capacityNum;

    if (num > 1024 * 1024 * 1024)
    {
        capacityNum << std::setiosflags(std::ios::fixed) << std::setprecision(4)
                    << ((double)num / (1024 * 1024 * 1024)) << " GB";
    }
    else if (num > 1024 * 1024)
    {
        capacityNum << std::setiosflags(std::ios::fixed) << std::setprecision(4)
                    << ((double)num / (1024 * 1024)) << " MB";
    }
    else if (num > 1024)
    {
        capacityNum << std::setiosflags(std::ios::fixed) << std::setprecision(4)
                    << ((double)num / (1024)) << " KB";
    }
    else
    {
        capacityNum << num << " B";
    }
    return capacityNum.str();
}
