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
 *  @date 20180921
 */
#pragma once

#include "Table.h"
#include <libprotocol/Protocol.h>
#include <libutilities/FixedBytes.h>

namespace bcos
{
namespace storage
{
class Storage : public std::enable_shared_from_this<Storage>
{
public:
    typedef std::shared_ptr<Storage> Ptr;

    virtual ~Storage(){};

    virtual Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition = nullptr) = 0;
    virtual size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) = 0;
    // Dicide if CachedStorage can commit modified part of Entries
    virtual bool onlyCommitDirty() { return false; };

    virtual void stop() {}
};

class StorageFactory : public std::enable_shared_from_this<StorageFactory>
{
public:
    typedef std::shared_ptr<StorageFactory> Ptr;
    virtual ~StorageFactory(){};
    virtual Storage::Ptr getStorage(const std::string& _dbName, bool _createIfMissing = false) = 0;
};

}  // namespace storage

}  // namespace bcos
