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

#include "RangeCachedStorage.h"

using namespace dev;
using namespace dev::storage;

void CachePage::import(Entries::Ptr entries)
{
    for (size_t i = 0; i < entries->size(); ++i)
    {
        auto entry = entries->get(i);
        addEntry(entry);
    }
}

void CachePage::addEntry(Entry::Ptr entry)
{
    m_entries.push_front(entry);
    auto it = m_entries.begin();
    m_ID2Entry.insert(std::make_pair(entry->getID(), it));
}

Entry::Ptr CachePage::getByID(int64_t id)
{
    auto it = m_ID2Entry.find(id);
    if (it != m_ID2Entry.end())
    {
        return *(it->second);
    }

    return Entry::Ptr();
}

void CachePage::removeByID(int64_t id)
{
    auto it = m_ID2Entry.find(id);
    if (it != m_ID2Entry.end())
    {
        m_entries.erase(it->second);
        m_ID2Entry.erase(it);
    }
}

void CachePage::setCondition(Condition::Ptr condition)
{
    m_condition = condition;
}

Condition::Ptr CachePage::condition()
{
    return m_condition;
}

void CachePage::setNum(int64_t num)
{
    m_num = num;
}

int64_t CachePage::num()
{
    return m_num;
}

Entries::Ptr CachePage::process(Condition::Ptr condition)
{
    Entries::Ptr out = std::make_shared<Entries>();

    for (auto it : m_entries)
    {
        if (condition->process(it))
        {
            out->addEntry(it);
        }
    }

    return out;
}

std::list<Entry::Ptr>* CachePage::entries()
{
    return &m_entries;
}

void CachePage::mergeFrom(CachePage::Ptr cachePage, bool move)
{
    for (auto entryIt = m_entries.begin(); entryIt != m_entries.end(); ++entryIt)
    {
        auto entry = cachePage->getByID((*entryIt)->getID());
        if (entry && entry->num() > (*entryIt)->num())
        {
            *entryIt = entry;

            if (move)
            {
                cachePage->removeByID(entry->getID());
            }
        }
    }
}

TableInfo::Ptr TableCache::tableInfo()
{
    return m_tableInfo;
}

std::vector<CachePage::Ptr>* TableCache::cachePages()
{
    return &m_cachePages;
}

CachePage::Ptr TableCache::tempPage()
{
    return m_tempPage;
}

Entries::Ptr RangeCachedStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    auto it = m_caches.find(tableInfo->name);
    if (it != m_caches.end())
    {
        for (auto cacheIt : *(it->second->cachePages()))
        {
            if (cacheIt->condition()->graterThan(condition))
            {
                return cacheIt->process(condition);
            }
        }

        if (it->second->tempPage()->condition()->graterThan(condition))
        {
            return it->second->tempPage()->process(condition);
        }
    }

    if (m_backend)
    {
        auto out = m_backend->select(hash, num, tableInfo, key, condition);

        auto tableIt = m_caches.find(tableInfo->name);
        if (tableIt == m_caches.end())
        {
            tableIt =
                m_caches.insert(std::make_pair(tableInfo->name, std::make_shared<TableCache>()))
                    .first;
        }

        auto cachePage = std::make_shared<CachePage>();
        cachePage->setCondition(condition);
        cachePage->import(out);

        for (auto pageIt = tableIt->second->cachePages()->begin();
             pageIt != tableIt->second->cachePages()->end(); ++pageIt)
        {
            if (condition->graterThan((*pageIt)->condition()))
            {
                pageIt = tableIt->second->cachePages()->erase(pageIt);
                continue;
            }

            if (condition->related((*pageIt)->condition()))
            {
                // related to page, merge related entry
                cachePage->mergeFrom(*pageIt);
            }
        }

        if (condition->related(tableIt->second->tempPage()->condition()))
        {
            cachePage->mergeFrom(tableIt->second->tempPage(), true);
        }

        tableIt->second->cachePages()->push_back(cachePage);
        return out;
    }

    return Entries::Ptr();
}

size_t RangeCachedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    (void)hash;
    (void)num;
    (void)datas;

#if 0
    for (auto it : datas)
    {
        auto tableIt = m_caches.find(it->tableName);
        if (tableIt == m_caches.end())
        {
            tableIt = m_caches.insert(std::make_pair(it->tableName, std::make_shared<TableCache>()))
                          .first;
        }

        for (size_t i = 0; i < it->entries->size(); ++i)
        {
            auto entry = it->entries->get(i);
            for (auto pageIt : *(tableIt->second->cachePages()))
            {
                if (pageIt->condition()->process(entry))
                {
                    pageIt->addEntry(entry);
                    break;
                }
            }
        }
    }
#endif

    return 0;
}
