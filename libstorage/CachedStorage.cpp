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
#include <libdevcore/easylog.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <thread>

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
    m_empty = false;
}

int64_t Cache::num() const
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

CachedStorage::CachedStorage()
{
    CACHED_STORAGE_LOG(INFO) << "Init flushStorage thread";
    m_taskThreadPool = std::make_shared<dev::ThreadPool>("FlushStorage", 1);

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

    m_running = std::make_shared<boost::atomic_bool>();
    m_running->store(true);
}

CachedStorage::~CachedStorage()
{
    STORAGE_LOG(INFO) << "Stoping flushStorage thread";
    m_taskThreadPool->stop();

    m_running->store(false);
    if (m_clearThread)
    {
        m_clearThread->join();
    }
}

Entries::Ptr CachedStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
#if 0
    CACHED_STORAGE_LOG(TRACE) << "Query data from cachedStorage table: " << tableInfo->name
                              << " key: " << key;
#endif

    auto out = std::make_shared<Entries>();

    auto result = selectNoCondition(hash, num, tableInfo, key, condition);

    Cache::Ptr caches = std::get<1>(result);
    // auto entries = caches->entriesPtr();
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

std::tuple<std::shared_ptr<Cache::RWScoped>, Cache::Ptr> CachedStorage::selectNoCondition(h256 hash,
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    (void)condition;

    auto result = touchCache(tableInfo, key, true);
    auto caches = std::get<1>(result);

    if (caches->empty())
    {
        if (m_backend)
        {
            auto conditionKey = std::make_shared<Condition>();
            conditionKey->EQ(tableInfo->key, key);
            auto backendData = m_backend->select(hash, num, tableInfo, key, conditionKey);

            CACHED_STORAGE_LOG(DEBUG) << tableInfo->name << ": " << key << " miss the cache";

            caches->setEntries(backendData);

            size_t totalCapacity = 0;
            for (auto it : *backendData)
            {
                totalCapacity += it->capacity();
            }
#if 0
            CACHED_STORAGE_LOG(TRACE) << "backend capacity: " << tableInfo->name << "-" << key
                                      << ", capacity: " << totalCapacity;
#endif
            touchMRU(tableInfo->name, key, totalCapacity);
        }
    }
    else
    {
        touchMRU(tableInfo->name, key, 0);
    }

    return std::make_tuple(std::get<0>(result), caches);
}

size_t CachedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    CACHED_STORAGE_LOG(INFO) << "CachedStorage commit: " << datas.size() << " hash: " << hash
                             << " num: " << num;

    tbb::atomic<size_t> total = 0;

    TIME_RECORD("Process dirty entries");
    std::shared_ptr<std::vector<TableData::Ptr>> commitDatas =
        std::make_shared<std::vector<TableData::Ptr>>();

    commitDatas->resize(datas.size());

    ssize_t currentStateIdx = -1;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, datas.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx)
            {
                auto requestData = datas[idx];
                auto commitData = std::make_shared<TableData>();
                commitData->info = requestData->info;
                commitData->dirtyEntries->resize(requestData->dirtyEntries->size());

                if (currentStateIdx < 0 && commitData->info->name == SYS_CURRENT_STATE)
                {
                    currentStateIdx = idx;
                }

                // addtion data
                std::set<std::string> addtionKey;
                tbb::spin_mutex addtionKeyMutex;

                tbb::parallel_for(tbb::blocked_range<size_t>(0, requestData->dirtyEntries->size()),
                    [&](const tbb::blocked_range<size_t>& rangeEntries) {
                        for (size_t i = rangeEntries.begin(); i < rangeEntries.end(); ++i)
                        {
                            ++total;

                            auto entry = requestData->dirtyEntries->get(i);
                            auto key = entry->getField(requestData->info->key);
                            auto id = entry->getID();

                            ssize_t change = 0;
                            if (id != 0)
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
                                            hash, num, requestData->info, key, conditionKey);

                                        CACHED_STORAGE_LOG(DEBUG)
                                            << requestData->info->name << "-" << key
                                            << " miss the cache while commit dirty entries";

                                        caches->setEntries(backendData);

                                        size_t totalCapacity = 0;
                                        for (auto it : *backendData)
                                        {
                                            totalCapacity += it->capacity();
                                        }
#if 0
                                        CACHED_STORAGE_LOG(TRACE)
                                            << "backend capacity: " << requestData->info->name
                                            << "-" << key << ", capacity: " << totalCapacity;
#endif
                                        touchMRU(requestData->info->name, key, totalCapacity);
                                    }
                                }

                                caches->setNum(num);

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
#if 0
                                    CACHED_STORAGE_LOG(TRACE)
                                        << "update capacity: " << commitData->info->name << "-"
                                        << key << ", from capacity: " << oldSize
                                        << " to capacity: " << (*entryIt)->capacity();
#endif
                                    change = (ssize_t)(
                                        (ssize_t)(*entryIt)->capacity() - (ssize_t)oldSize);

                                    (*entryIt)->setNum(num);

                                    auto commitEntry = std::make_shared<Entry>();
                                    commitEntry->copyFrom(*entryIt);
                                    (*commitData->dirtyEntries)[i] = commitEntry;

                                    if (m_backend && !m_backend->onlyDirty())
                                    {
                                        tbb::spin_mutex::scoped_lock lock(addtionKeyMutex);
                                        auto inserted = addtionKey.insert(key).second;

                                        if (inserted)
                                        {
                                            for (auto it = caches->entries()->begin();
                                                 it != caches->entries()->end(); ++it)
                                            {
                                                if (it != entryIt)
                                                {
                                                    commitData->dirtyEntries->addEntry(*it);
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    CACHED_STORAGE_LOG(FATAL)
                                        << "Can not find entry in cache, id:" << entry->getID()
                                        << " key:" << key;

                                    // impossible
                                    BOOST_THROW_EXCEPTION(StorageException(
                                        -1, "Can not find entry in cache, id: " +
                                                boost::lexical_cast<std::string>(entry->getID())));
                                }
                            }
                            else
                            {
                                CACHED_STORAGE_LOG(FATAL)
                                    << "Dirty entry id equal to 0, id: " << id << " key: " << key;
                                BOOST_THROW_EXCEPTION(StorageException(
                                    -1, "Dirty entry id equal to 0, id: " +
                                            boost::lexical_cast<std::string>(entry->getID())));
                            }

                            touchMRU(requestData->info->name, key, change);
                        }
                    });

                // TODO: check if necessery
                tbb::parallel_sort(commitData->dirtyEntries->begin(),
                    commitData->dirtyEntries->end(), EntryLessNoLock(requestData->info));

                commitData->newEntries->shallowFrom(requestData->newEntries);

                TIME_RECORD("Sort new entries");
                tbb::parallel_sort(commitData->newEntries->begin(), commitData->newEntries->end(),
                    EntryLessNoLock(requestData->info));

                (*commitDatas)[idx] = commitData;
            }
        });

    TIME_RECORD("Process new entries");
    for (size_t i = 0; i < commitDatas->size(); ++i)
    {
        auto commitData = (*commitDatas)[i];
        for (size_t j = 0; j < commitData->newEntries->size(); ++j)
        {
            auto commitEntry = commitData->newEntries->get(j);
            commitEntry->setID(++m_ID);
            commitEntry->setNum(num);
#if 0
            STORAGE_LOG(TRACE) << "Set new entry ID: " << m_ID;
#endif
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
                            m_backend->select(hash, num, commitData->info, key, conditionKey);

                        CACHED_STORAGE_LOG(DEBUG) << commitData->info->name << "-" << key
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
                }

                caches->entries()->addEntry(cacheEntry);
                caches->setNum(num);
            }
#if 0
            STORAGE_LOG(TRACE) << "new cached: " << commitData->info->name << "-" << key
                               << ", capacity: " << cacheEntry->capacity();
#endif
            touchMRU(commitData->info->name, key, cacheEntry->capacity());
        }
    }

    if (m_backend)
    {
        TIME_RECORD("Submit commit task");
        // new task write to backend
        Task::Ptr task = std::make_shared<Task>();
        task->hash = hash;
        task->num = num;
        task->datas = commitDatas;

        TableData::Ptr data;
        if (currentStateIdx < 0)
        {
            data = std::make_shared<TableData>();
            data->info->name = SYS_CURRENT_STATE;
            data->info->key = SYS_KEY;
            data->info->fields = std::vector<std::string>{"value"};
        }
        else
        {
            data = (*commitDatas)[currentStateIdx];
        }

        Entry::Ptr idEntry = std::make_shared<Entry>();
        idEntry->setID(1);
        idEntry->setNum(num);
        idEntry->setStatus(0);
        idEntry->setField(SYS_KEY, SYS_KEY_CURRENT_ID);
        idEntry->setField("value", boost::lexical_cast<std::string>(m_ID));

        data->dirtyEntries->addEntry(idEntry);

        task->datas->push_back(data);
        auto backend = m_backend;
        auto self = std::weak_ptr<CachedStorage>(
            std::dynamic_pointer_cast<CachedStorage>(shared_from_this()));

        m_commitNum.store(num);
        m_taskThreadPool->enqueue([backend, task, self]() {
            auto now = std::chrono::system_clock::now();
            STORAGE_LOG(INFO) << "Start commit block: " << task->num << " to backend storage";

            try
            {
                backend->commit(task->hash, task->num, *(task->datas));

                auto storage = self.lock();
                if (storage)
                {
                    storage->setSyncNum(task->num);

                    std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - now;
                    STORAGE_LOG(INFO) << "[g:" << std::to_string(storage->groupID()) << "]"
                                      << "\n-------------------------------------------------------"
                                         "--------------\n"
                                      << "Commit block: " << task->num
                                      << " to backend storage finished, current cached block: "
                                      << storage->m_commitNum << "\n"
                                      << "Flush elapsed time: " << std::setiosflags(std::ios::fixed)
                                      << std::setprecision(4) << elapsed.count() << "s"
                                      << "\n-------------------------------------------------------"
                                         "--------------\n";
                }
            }
            catch (std::exception& e)
            {
                LOG(FATAL) << "access storage failed exit errmsg:" << e.what();
                raise(SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            }
        });

        STORAGE_LOG(INFO) << "Submited block task: " << num
                          << ", current syncd block: " << m_syncNum;

        uint64_t waitCount = 0;
        while ((size_t)(m_commitNum - m_syncNum) > m_maxForwardBlock)
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
        STORAGE_LOG(INFO) << "No backend storage, skip commit...";

        setSyncNum(num);
    }
    return total;
}

bool CachedStorage::onlyDirty()
{
    return true;
}

void CachedStorage::setBackend(Storage::Ptr backend)
{
    m_backend = backend;
}

void CachedStorage::init()
{
    auto tableInfo = std::make_shared<storage::TableInfo>();
    tableInfo->name = SYS_CURRENT_STATE;
    tableInfo->key = SYS_KEY;
    tableInfo->fields = std::vector<std::string>{"value"};

    auto condition = std::make_shared<Condition>();
    condition->EQ(SYS_KEY, SYS_KEY_CURRENT_ID);

    // get id from backend
    auto out = m_backend->select(h256(), 0, tableInfo, SYS_KEY_CURRENT_ID, condition);
    if (out->size() > 0)
    {
        auto entry = out->get(0);
        auto numStr = entry->getField(SYS_VALUE);
        m_ID = boost::lexical_cast<size_t>(numStr);
    }

    startClearThread();
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

size_t CachedStorage::ID()
{
    return m_ID;
}

void CachedStorage::startClearThread()
{
    std::weak_ptr<CachedStorage> self(std::dynamic_pointer_cast<CachedStorage>(shared_from_this()));
    auto running = m_running;
    m_clearThread = std::make_shared<std::thread>([running, self]() {
        while (*running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            auto storage = self.lock();
            if (storage)
            {
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
        // RWMutexScoped lockCache(m_cachesMutex, false);
        auto result = m_caches.insert(std::make_pair(cacheKey, cache));
        cache = result.first->second;

        inserted = result.second;
    }

    auto cacheLock = std::make_shared<Cache::RWScoped>(*(cache->mutex()), write);
    auto locked = true;
    if (inserted && locked)
    {
        hit = false;

        cache->setKey(key);
        cache->setTableInfo(tableInfo);
    }

    if (hit)
    {
        ++m_hitTimes;
    }

    return std::make_tuple(cacheLock, cache, locked);
}

void CachedStorage::removeCache(const std::string& table, const std::string& key)
{
    RWMutexScoped lockCache(m_cachesMutex, true);

    auto cacheKey = table + "_" + key;
    m_caches.unsafe_erase(cacheKey);
    // m_caches.erase(cacheKey);
}

void CachedStorage::checkAndClear()
{
    uint64_t count = 0;
    while (count < m_maxPopMRU)
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
            if (m_capacity > m_maxCapacity && !m_mru->empty())
            {
#if 0
                CACHED_STORAGE_LOG(TRACE)
                    << "Current capacity: " << m_capacity
                    << " greater than max capacity: " << m_maxCapacity << ", clearing...";
#endif
                needClear = true;
            }
        }

        if (needClear)
        {
            for (auto it = m_mru->begin(); it != m_mru->end();)
            {
                if (m_capacity <= (int64_t)m_maxCapacity || m_mru->empty())
                {
                    break;
                }

                ++clearThrough;

                auto tableInfo = std::make_shared<TableInfo>();
                tableInfo->name = it->first;

                auto result = touchCache(tableInfo, it->second);
                auto cache = std::get<1>(result);
                if (std::get<2>(result))
                {
                    if (m_syncNum > 0 && (cache->num() <= m_syncNum))
                    {
#if 0
                        CACHED_STORAGE_LOG(TRACE)
                            << "Clear last recent record: " << it->first << "-" << it->second;
#endif

                        int64_t totalCapacity = 0;
                        for (auto entryIt : *(cache->entries()))
                        {
#if 0
                            CACHED_STORAGE_LOG(TRACE)
                                << "entry remove capacity: " << it->first << "-" << it->second
                                << ", capacity: " << entryIt->capacity();
#endif
                            totalCapacity += entryIt->capacity();
                        }

                        ++clearCount;
#if 0
                        CACHED_STORAGE_LOG(TRACE) << "remove capacity: " << it->first << "-"
                                                  << it->second << ", capacity: " << totalCapacity
                                                  << ", current cache size: " << m_mru->size();
#endif
                        updateCapacity(0 - totalCapacity);

                        removeCache(it->first, it->second);
                        it = m_mru->erase(it);
                    }
                }
                else
                {
                    ++it;
                }

                if (m_capacity <= m_maxCapacity || m_mru->empty())
                {
                    break;
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
                                 << "Current total cached entries: " << m_mru->size()
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
    // auto oldValue = m_capacity.fetch_and_add(capacity);
    m_capacity.fetch_add(capacity);

#if 0
    CACHED_STORAGE_LOG(TRACE) << "Capacity change by: " << (capacity) << " , from: " << oldValue
                              << " to: " << m_capacity;
#endif
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
