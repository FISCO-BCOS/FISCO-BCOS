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

namespace dev
{
namespace storage
{
class CachePage : public std::enable_shared_from_this<CachePage>
{
public:
    typedef std::shared_ptr<CachePage> Ptr;
    virtual ~CachePage(){};

    virtual void import(Entries::Ptr entries);
    virtual void addEntry(Entry::Ptr entry);
    virtual Entry::Ptr getByID(int64_t id);
    virtual void removeByID(int64_t id);
    virtual void setCondition(Condition::Ptr condition);
    virtual Condition::Ptr condition();
    virtual void setNum(int64_t num);
    virtual int64_t num();
    virtual Entries::Ptr process(Condition::Ptr condition);

    virtual std::list<Entry::Ptr>* entries();
    virtual void mergeFrom(CachePage::Ptr cachePage, bool move = false);

private:
    std::list<Entry::Ptr> m_entries;
    std::map<int64_t, std::list<Entry::Ptr>::iterator> m_ID2Entry;
    // std::map<std::string, std::list<Entry::Ptr>::iterator >

    Condition::Ptr m_condition;
    int64_t m_num = 0;
};

class TableCache : public std::enable_shared_from_this<TableCache>
{
public:
    typedef std::shared_ptr<TableCache> Ptr;
    virtual ~TableCache(){};

    virtual TableInfo::Ptr tableInfo();
    virtual std::vector<CachePage::Ptr>* cachePages();
    virtual CachePage::Ptr tempPage();

private:
    TableInfo::Ptr m_tableInfo;
    std::vector<CachePage::Ptr> m_cachePages;
    CachePage::Ptr m_tempPage;
};

class RangeCachedStorage : public Storage
{
public:
    typedef std::shared_ptr<RangeCachedStorage> Ptr;

    virtual ~RangeCachedStorage(){};

    Entries::Ptr select(h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) override;
    size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) override;
    bool onlyDirty() override;

private:
    std::map<std::string, TableCache::Ptr> m_caches;
    Storage::Ptr m_backend;
};

}  // namespace storage

}  // namespace dev
