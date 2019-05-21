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

Caches::Caches()
{
    m_entries = std::make_shared<Entries>();
    m_num.store(0);
}

std::string Caches::key()
{
    return m_key;
}

void Caches::setKey(const std::string& key)
{
    m_key = key;
}

Entries::Ptr Caches::entries()
{
    return m_entries;
}

Entries* Caches::entriesPtr()
{
    return m_entries.get();
}

void Caches::setEntries(Entries::Ptr entries)
{
    m_entries = entries;
    m_empty = false;
}

int64_t Caches::num() const
{
    return m_num;
}

void Caches::setNum(int64_t num)
{
    m_num = num;
}

Caches::RWMutex* Caches::mutex()
{
    return &m_mutex;
}

bool Caches::empty()
{
    return m_empty;
}

void Caches::setEmptry(bool empty)
{
    m_empty = empty;
}

TableInfo::Ptr TableCaches::tableInfo()
{
    return m_tableInfo;
}

void TableCaches::setTableInfo(TableInfo::Ptr tableInfo)
{
    m_tableInfo = tableInfo;
}

Caches::Ptr TableCaches::findCache(const std::string& key)
{
    auto it = m_caches.find(key);
    if (it != m_caches.end())
    {
        return it->second;
    }
    else
    {
        return Caches::Ptr();
    }
}

std::pair<tbb::concurrent_unordered_map<std::string, Caches::Ptr>::iterator, bool>
TableCaches::addCache(const std::string& key, Caches::Ptr cache)
{
    auto it = m_caches.insert(std::make_pair(key, cache));
    return it;
}

void TableCaches::removeCache(const std::string& key)
{
    auto it = m_caches.find(key);
    if (it != m_caches.end())
    {
        m_caches.unsafe_erase(it);
    }
}

tbb::concurrent_unordered_map<std::string, Caches::Ptr>* TableCaches::caches()
{
    return &m_caches;
}

CachedStorage::CachedStorage()
{
    CACHED_STORAGE_LOG(INFO) << "Init flushStorage thread";
    m_taskThreadPool = std::make_shared<dev::ThreadPool>("FlushStorage", 1);
    m_syncNum.store(0);
    m_commitNum.store(0);
    m_capacity.store(0);
}

CachedStorage::~CachedStorage()
{
    STORAGE_LOG(INFO) << "Stoping flushStorage thread";
    m_taskThreadPool->stop();
}

Entries::Ptr CachedStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    CACHED_STORAGE_LOG(TRACE) << "Query data from cachedStorage table: " << tableInfo->name
                              << " key: " << key;

    checkAndClear();

    auto out = std::make_shared<Entries>();

    auto result = selectNoCondition(hash, num, tableInfo, key, condition);

    Caches::Ptr caches = std::get<0>(result);
    auto entries = caches->entriesPtr();
    for (size_t i = 0; i < entries->size(); ++i)
    {
        auto entry = entries->get(i);
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

std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped>> CachedStorage::selectNoCondition(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    (void)condition;

    auto result = touchCache(tableInfo, key);
    auto caches = std::get<0>(result);
    caches->setNum(num);

    if (caches->empty())
    {
        if (m_backend)
        {
            auto conditionKey = std::make_shared<Condition>();
            conditionKey->EQ(tableInfo->key, key);
            auto backendData = m_backend->select(hash, num, tableInfo, key, conditionKey);

            CACHED_STORAGE_LOG(DEBUG) << tableInfo->name << "-" << key << " miss the cache";

            caches->setEntries(backendData);

            size_t totalCapacity = 0;
            for (auto it : *backendData)
            {
                totalCapacity += it->capacity();
            }
            CACHED_STORAGE_LOG(TRACE) << "backend capacity: " << tableInfo->name << "-" << key
                                      << ", capacity: " << totalCapacity;

            touchMRU(tableInfo->name, key, totalCapacity);
        }
    }
    else
    {
        touchMRU(tableInfo->name, key, 0);
    }

    return std::make_tuple(caches, std::get<1>(result));
}

size_t CachedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    tbb::spin_mutex::scoped_lock lock(m_cachesMutex);

    CACHED_STORAGE_LOG(INFO) << "CachedStorage commit: " << datas.size() << " hash: " << hash
                             << " num: " << num;

    tbb::atomic<size_t> total = 0;

    TIME_RECORD("Process dirty entries");
    std::shared_ptr<std::vector<TableData::Ptr>> commitDatas =
        std::make_shared<std::vector<TableData::Ptr>>();

    commitDatas->resize(datas.size());

    ssize_t currentStateIdx = -1;

#if 0
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, datas.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx)
            {
#endif
    for (size_t idx = 0; idx < datas.size(); ++idx)
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

#if 0
                tbb::parallel_for(tbb::blocked_range<size_t>(0, requestData->dirtyEntries->size()),
                    [&](const tbb::blocked_range<size_t>& rangeEntries) {
                        for (size_t i = rangeEntries.begin(); i < rangeEntries.end(); ++i)
#endif
        for (size_t i = 0; i < requestData->dirtyEntries->size(); ++i)
        {
            ++total;

            auto entry = requestData->dirtyEntries->get(i);
            auto key = entry->getField(requestData->info->key);
            auto id = entry->getID();

            ssize_t change = 0;
            if (id != 0)
            {
                auto result = touchCacheNoLock(requestData->info, key, true);
                auto caches = std::get<0>(result);
                if (caches->empty())
                {
                    if (m_backend)
                    {
                        auto conditionKey = std::make_shared<Condition>();
                        conditionKey->EQ(requestData->info->key, key);
                        auto backendData =
                            m_backend->select(hash, num, requestData->info, key, conditionKey);

                        CACHED_STORAGE_LOG(DEBUG) << requestData->info->name << "-" << key
                                                  << " miss the cache while commit dirty entries";

                        caches->setEntries(backendData);

                        size_t totalCapacity = 0;
                        for (auto it : *backendData)
                        {
                            totalCapacity += it->capacity();
                        }
                        CACHED_STORAGE_LOG(TRACE) << "backend capacity: " << requestData->info->name
                                                  << "-" << key << ", capacity: " << totalCapacity;

                        touchMRU(requestData->info->name, key, totalCapacity);
                    }
                }

                caches->setNum(num);

                auto entryIt =
                    std::lower_bound(caches->entries()->begin(), caches->entries()->end(), entry,
                        [](const Entry::Ptr& lhs, const Entry::Ptr& rhs) {
                            return lhs->getID() < rhs->getID();
                        });

                if (entryIt != caches->entries()->end() && (*entryIt)->getID() == id)
                {
                    auto oldSize = (*entryIt)->capacity();

                    for (auto fieldIt : *entry->fields())
                    {
                        (*entryIt)->setField(fieldIt.first, fieldIt.second);
                    }

                    CACHED_STORAGE_LOG(TRACE) << "update capacity: " << commitData->info->name
                                              << "-" << key << ", from capacity: " << oldSize
                                              << " to capacity: " << (*entryIt)->capacity();

                    change = (ssize_t)((ssize_t)(*entryIt)->capacity() - (ssize_t)oldSize);

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
                        << "Can not find entry in cache, id:" << entry->getID() << " key:" << key;

                    // impossible
                    BOOST_THROW_EXCEPTION(
                        StorageException(-1, "Can not find entry in cache, id: " +
                                                 boost::lexical_cast<std::string>(entry->getID())));
                }
            }
            else
            {
                CACHED_STORAGE_LOG(FATAL)
                    << "Dirty entry id equal to 0, id: " << id << " key: " << key;
                BOOST_THROW_EXCEPTION(
                    StorageException(-1, "Dirty entry id equal to 0, id: " +
                                             boost::lexical_cast<std::string>(entry->getID())));
            }

            touchMRU(requestData->info->name, key, change);
        }
#if 0
                    });
#endif

        tbb::parallel_sort(commitData->dirtyEntries->begin(), commitData->dirtyEntries->end(),
            EntryLessNoLock(requestData->info));

        commitData->newEntries->shallowFrom(requestData->newEntries);
        tbb::parallel_sort(commitData->newEntries->begin(), commitData->newEntries->end(),
            EntryLessNoLock(requestData->info));

        (*commitDatas)[idx] = commitData;
    }
#if 0
        });
#endif

    TIME_RECORD("Process new entries");
    for (size_t i = 0; i < commitDatas->size(); ++i)
    {
        auto commitData = (*commitDatas)[i];
        for (size_t j = 0; j < commitData->newEntries->size(); ++j)
        {
            auto commitEntry = commitData->newEntries->get(j);
            commitEntry->setID(++m_ID);
            commitEntry->setNum(num);

            STORAGE_LOG(TRACE) << "Set new entry ID: " << m_ID;

            ++total;

            auto key = commitEntry->getField(commitData->info->key);

            auto cacheEntry = std::make_shared<Entry>();
            cacheEntry->copyFrom(commitEntry);

            if (cacheEntry->force())
            {
                auto result = touchCacheNoLock(commitData->info, key, true);

                auto caches = std::get<0>(result);
                caches->setNum(num);
                caches->entries()->addEntry(cacheEntry);
                caches->setEmptry(false);
            }
            else
            {
                auto result = touchCacheNoLock(commitData->info, key, true);
                auto caches = std::get<0>(result);
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
                        CACHED_STORAGE_LOG(TRACE) << "backend capacity: " << commitData->info->name
                                                  << "-" << key << ", capacity: " << totalCapacity;

                        touchMRU(commitData->info->name, key, totalCapacity);
                    }
                }

                caches->entries()->addEntry(cacheEntry);
                caches->setNum(num);
            }

            STORAGE_LOG(TRACE) << "new cached: " << commitData->info->name << "-" << key
                               << ", capacity: " << cacheEntry->capacity();

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
            backend->commit(task->hash, task->num, *(task->datas));

            auto storage = self.lock();
            if (storage)
            {
                storage->setSyncNum(task->num);

                std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - now;
                STORAGE_LOG(INFO)
                    << "[g:" << std::to_string(storage->groupID()) << "]"
                    << "\n---------------------------------------------------------------------\n"
                    << "Commit block: " << task->num
                    << " to backend storage finished, current cached block: "
                    << storage->m_commitNum << "\n"
                    << "Flush elapsed time: " << std::setiosflags(std::ios::fixed)
                    << std::setprecision(4) << elapsed.count() << "s"
                    << "\n---------------------------------------------------------------------\n";
            }
        });

        STORAGE_LOG(INFO) << "Submited block task: " << num
                          << ", current syncd block: " << m_syncNum;
    }
    else
    {
        STORAGE_LOG(INFO) << "No backend storage, skip commit...";
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

void CachedStorage::touchMRU(std::string table, std::string key, ssize_t capacity)
{
    tbb::spin_mutex::scoped_lock lock(m_mruMutex);

    if (capacity != 0)
    {
        updateCapacity(capacity);
    }

    auto r = m_mru.push_back(std::make_pair(table, key));
    if (!r.second)
    {
        m_mru.relocate(m_mru.end(), r.first);
    }
}

std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped>> CachedStorage::touchCache(
    TableInfo::Ptr tableInfo, std::string key, bool write)
{
    tbb::spin_mutex::scoped_lock lock(m_cachesMutex);

    return touchCacheNoLock(tableInfo, key, write);
}

std::tuple<Caches::Ptr, std::shared_ptr<Caches::RWScoped>> CachedStorage::touchCacheNoLock(
    TableInfo::Ptr tableInfo, std::string key, bool write)
{
    tbb::spin_mutex::scoped_lock lock(m_touchMutex);
    bool hit = true;

    ++m_queryTimes;

    auto tableIt = m_caches.find(tableInfo->name);
    if (tableIt == m_caches.end())
    {
        hit = false;
        tableIt =
            m_caches.insert(std::make_pair(tableInfo->name, std::make_shared<TableCaches>())).first;

        tableIt->second->setTableInfo(tableInfo);
    }

    auto tableCaches = tableIt->second;
    auto caches = tableCaches->findCache(key);
    if (!caches)
    {
        hit = false;
        auto newCache = std::make_shared<Caches>();
        newCache->setKey(key);

        caches = tableCaches->addCache(key, newCache).first->second;
    }

    if (hit)
    {
        ++m_hitTimes;
    }

    return std::make_tuple(caches, std::make_shared<Caches::RWScoped>(*(caches->mutex()), write));
}

void CachedStorage::checkAndClear()
{
    {
        tbb::spin_mutex::scoped_lock lock(m_clearMutex);

        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapseds = now - m_lastClear;
        if (elapseds.count() > 1.0)
        {
            m_lastClear = now;
        }
        else
        {
            return;
        }
    }

    tbb::spin_mutex::scoped_lock lock(m_cachesMutex);
    tbb::spin_mutex::scoped_lock lockMRU(m_mruMutex);

    TIME_RECORD("Check and clear");

    bool needClear = false;
    size_t clearTimes = 0;

    auto currentCapacity = m_capacity;

    size_t clearCount = 0;
    size_t clearThrough = 0;
    do
    {
        needClear = false;

        if (clearTimes > 5)
        {
            size_t sleepTimes = clearTimes < 100 ? clearTimes * 10 : 100 * 10;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimes));
        }

        if (m_syncNum > 0)
        {
            if ((size_t)(m_commitNum - m_syncNum) > m_maxForwardBlock)
            {
                CACHED_STORAGE_LOG(INFO)
                    << "Current block number: " << m_commitNum
                    << " greater than syncd block number: " << m_syncNum << ", waiting...";
                needClear = true;
            }
            else if (m_capacity > (int64_t)m_maxCapacity && !m_mru.empty())
            {
                CACHED_STORAGE_LOG(TRACE)
                    << "Current capacity: " << m_capacity
                    << " greater than max capacity: " << m_maxCapacity << ", waiting...";
                needClear = true;
            }
        }

        if (needClear)
        {
            for (auto it = m_mru.begin(); it != m_mru.end(); ++it)
            {
                ++clearThrough;
                auto tableIt = m_caches.find(it->first);
                if (tableIt != m_caches.end())
                {
                    auto cache = tableIt->second->findCache(it->second);
                    if (cache)
                    {
                        Caches::RWScoped(*(cache->mutex()), false);

                        if (m_syncNum > 0 && ((size_t)cache->num() <= m_syncNum))
                        {
                            CACHED_STORAGE_LOG(TRACE)
                                << "Clear last recent record: "
                                << tableIt->second->tableInfo()->name << "-" << it->second;

                            size_t totalCapacity = 0;
                            for (auto entryIt : *(cache->entries()))
                            {
                                CACHED_STORAGE_LOG(TRACE)
                                    << "entry remove capacity: "
                                    << tableIt->second->tableInfo()->name << "-" << it->second
                                    << ", capacity: " << entryIt->capacity();
                                totalCapacity += entryIt->capacity();
                            }

                            ++clearCount;
                            CACHED_STORAGE_LOG(TRACE)
                                << "remove capacity: " << tableIt->second->tableInfo()->name << "-"
                                << it->second << ", capacity: " << totalCapacity
                                << ", current cache size: " << m_mru.size();
                            updateCapacity(0 - (ssize_t)totalCapacity);

                            tableIt->second->removeCache(it->second);
                            it = m_mru.erase(it);
                        }
                    }
                    else
                    {
                        CACHED_STORAGE_LOG(FATAL)
                            << "Cache not found, erase mru: " << it->first << "-" << it->second;
                        it = m_mru.erase(it);
                    }

                    if (tableIt->second->caches()->empty())
                    {
                        m_caches.erase(tableIt);
                    }
                }
                else
                {
                    CACHED_STORAGE_LOG(FATAL)
                        << "Table not found, erase mru: " << it->first << "-" << it->second;
                    it = m_mru.erase(it);
                }

                if (m_capacity <= (int64_t)m_maxCapacity || m_mru.empty())
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
                                 << "Current total cached entries: " << m_mru.size()
                                 << ", total capacaity: " << readableCapacity(m_capacity);

        CACHED_STORAGE_LOG(DEBUG) << "Cache Status: \n\n"
                                  << "Total query: " << m_queryTimes << "\n"
                                  << "Total cache hit: " << m_hitTimes << "\n"
                                  << "Total cache miss: " << m_queryTimes - m_hitTimes << "\n"
                                  << "Total hit ratio: " << std::setiosflags(std::ios::fixed)
                                  << std::setprecision(4)
                                  << ((double)m_hitTimes / m_queryTimes) * 100 << "%"
                                  << "\n\n"
                                  << "Cache capacity: " << readableCapacity(m_capacity) << "\n"
                                  << "Cache size: " << m_mru.size();
    }
}

void CachedStorage::updateCapacity(ssize_t capacity)
{
    auto oldValue = m_capacity.fetch_and_add(capacity);
    CACHED_STORAGE_LOG(TRACE) << "Capacity change by: " << (capacity) << " , from: " << oldValue
                              << " to: " << m_capacity;
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
