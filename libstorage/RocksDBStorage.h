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
/** @file RocksDBStorage.h
 *  @author xingqiangbai
 *  @date 20180423
 */
#pragma once

#include "Storage.h"
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <tbb/spin_mutex.h>
#include <map>
//namespace 声明将代码封装在不同的命名空间中，以组织相关功能并防止名称冲突。这里有三个命名空间：rocksdb、dev 和 storage。
namespace rocksdb
{
class DB;
}
namespace dev
{
namespace storage
{
class BasicRocksDB;


//RocksDBStorage 类从一个名为 Storage 的基类继承而来，这暗示它在存储层次结构中扮演一种特定角色。
class RocksDBStorage : public Storage
{
public:
    typedef std::shared_ptr<RocksDBStorage> Ptr;
    //类的构造函数接受两个布尔类型的参数 _disableWAL 和 _shouldCompleteDirty，它们分别用于初始化成员变量 m_disableWAL 和 m_shouldCompleteDirty。
    RocksDBStorage(bool _disableWAL = false, bool _shouldCompleteDirty = false)
      : m_disableWAL(_disableWAL), m_shouldCompleteDirty(_shouldCompleteDirty)
    {}
    virtual ~RocksDBStorage(){};
    //select 函数用于选择数据条目，它接受一个名为 num 的整数、一个 TableInfo::Ptr 类型的指针 tableInfo、一个字符串 key 和一个 Condition::Ptr 类型的指针 condition。
    //它返回一个名为 Entries::Ptr 的指针，可能是一种数据结构。
    Entries::Ptr select(int64_t num, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override;
    //commit 函数用于提交数据更改，它接受一个整数 num 和一个 TableData::Ptr 对象的向量 datas，并返回一个 size_t 类型的值，可能表示提交的更改数量
    size_t commit(int64_t num, const std::vector<TableData::Ptr>& datas) override;
    //setDB 函数用于设置一个指向 BasicRocksDB 对象的共享指针，这将在类的操作中用到。
    void setDB(std::shared_ptr<BasicRocksDB> db) { m_db = db; }

private:
    bool m_disableWAL = false;
    bool m_shouldCompleteDirty = false;
    //processEntries 和 processDirtyEntries 是用于处理数据条目的私有成员函数。它们可能根据提供的参数对数据进行处理。
    void processEntries(int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries, bool isDirtyEntries);

    void processDirtyEntries(int64_t num,
        std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
            key2value,
        TableInfo::Ptr tableInfo, Entries::Ptr entries);
    //m_db 是一个指向 BasicRocksDB 对象的共享指针，它似乎是类的一个重要依赖。
    std::shared_ptr<BasicRocksDB> m_db;
    //m_writeBatchMutex 是一个 tbb::spin_mutex 类型的变量，用于在线程之间实现同步，以确保数据的并发访问安全。
    tbb::spin_mutex m_writeBatchMutex;
};

}  // namespace storage

}  // namespace dev
