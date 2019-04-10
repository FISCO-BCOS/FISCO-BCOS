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

#include "Table.h"
#include <libdevcore/FixedHash.h>

namespace dev
{
namespace storage
{

class CachePage: std::enable_shared_from_this<CachePage> {
	typedef std::shared_ptr<CachePage> Ptr;

	Entries::Ptr m_entries;
	Condition::Ptr m_condition;
	int64_t m_flushNum;
};

class TableCache {
	typedef std::shared_ptr<TableCache> Ptr;

	std::vector<CachePage::Ptr> m_cachePages;
};

class RangeCachedStorage : public std::enable_shared_from_this<RangeCachedStorage>
{
public:
    typedef std::shared_ptr<RangeCachedStorage> Ptr;

    virtual ~RangeCachedStorage(){};

    virtual Entries::Ptr select(h256 hash, int num, const std::string& table,
        const std::string& key, Condition::Ptr condition = nullptr) override;
    virtual size_t commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas,
        h256 const& blockHash) override;
    virtual bool onlyDirty() override;

private:
    std::map<std::string, TableCache::Ptr> m_caches;
};

}

}
