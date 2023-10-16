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
/** @file RocksDBStorage.cpp
 *  @author xingqiangbai
 *  @date 20180423
 */

#include "RocksDBStorage.h"
#include "BasicRocksDB.h"
#include "StorageException.h"
#include "Table.h"
#include "boost/archive/binary_iarchive.hpp"
#include "boost/archive/binary_oarchive.hpp"
#include "boost/serialization/map.hpp"
#include "boost/serialization/serialization.hpp"
#include "boost/serialization/vector.hpp"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/write_batch.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <tbb/parallel_for.h>
#include <memory>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::storage;
using namespace rocksdb;
//select 函数用于根据给定参数从 RocksDB 数据库中选择数据条目。参数包括数据库操作号 num、表信息 tableInfo、键 key，以及条件 condition。
Entries::Ptr RocksDBStorage::select(
    int64_t, TableInfo::Ptr tableInfo, const string& key, Condition::Ptr condition)
{   
    try
    {
        cout<<"RocksDBStorage::select方法"<<"   "<<"入口参数："<<tableInfo->name<<"     "<<key<<endl;
       //首先，根据传入的参数构建数据条目的键 entryKey，该键由表信息的名称和给定的键 key 组成。
        string entryKey = tableInfo->name;
        entryKey.append("_").append(key);

        string value;
        //然后，使用 m_db->Get 方法从 RocksDB 数据库中获取与键对应的值。
        auto s = m_db->Get(ReadOptions(), entryKey, value);
        
        //如果获取操作失败，并且不是因为未找到数据的情况，会记录错误信息，并抛出 StorageException 异常。
        if (!s.ok() && !s.IsNotFound())
        {
            STORAGE_ROCKSDB_LOG(ERROR)
                << LOG_DESC("Query rocksdb failed") << LOG_KV("status", s.ToString());

            BOOST_THROW_EXCEPTION(StorageException(-1, "Query rocksdb exception:" + s.ToString()));
        }
    
        Entries::Ptr entries = make_shared<Entries>();//创建一个名为 entries 的 Entries::Ptr 实例，用于存储选定的数据条目。
        //如果未找到数据（IsNotFound），则返回一个空的 Entries::Ptr 实例，表示没有找到相应的数据。
        if (!s.IsNotFound())
        {
            vector<map<string, string>> res;
            stringstream ss(value);
            //否则，从获取到的值中解析数据，这里使用了 boost::archive::binary_iarchive
            boost::archive::binary_iarchive ia(ss);
            ia >> res;
            //遍历 res 中的每个数据条目，构造一个名为 entry 的 Entry::Ptr 实例
            for (auto it = res.begin(); it != res.end(); ++it)
            {  // TODO: use tbb parallel_for
                Entry::Ptr entry = make_shared<Entry>();
                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {   //对于每个字段，检查其键是否为特定的字段（如 ID_FIELD、NUM_FIELD、STATUS），并将对应的值设置到 entry 实例中。
                    if (valueIt->first == ID_FIELD)
                    {//如果是 ID_FIELD，则调用 entry->setID(valueIt->second)，将对应的值设置为数据条目的 ID。
                        entry->setID(valueIt->second);
                    }
                    else if (valueIt->first == NUM_FIELD)
                    {//如果是 NUM_FIELD，则调用 entry->setNum(valueIt->second)，将对应的值设置为数据条目的编号。
                        entry->setNum(valueIt->second);
                    }
                    else if (valueIt->first == STATUS)
                    {//如果是 STATUS，则调用 entry->setStatus(valueIt->second)，将对应的值设置为数据条目的状态。
                        entry->setStatus(valueIt->second);
                    }
                    else
                    {//如果是其他字段，调用 entry->setField(valueIt->first, valueIt->second)，将字段名和对应的值添加到数据条目的其他字段中。
                        entry->setField(valueIt->first, valueIt->second);
                    }
                }
                //如果 entry 的状态为正常且满足条件（或没有条件），则将 entry 添加到 entries 实例中。
                if (entry->getStatus() == Entry::Status::NORMAL &&
                    (!condition || condition->process(entry)))
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }

        return entries;
    }
    catch (DatabaseNeedRetry const& e)//如果捕获到类型为 DatabaseNeedRetry 的异常 e，表示数据库操作需要重试
    {
        STORAGE_ROCKSDB_LOG(WARNING) << LOG_DESC("Query rocksdb exception, need to retry again ")
                                     << LOG_KV("msg", boost::diagnostic_information(e));
    }
    catch (exception& e)//如果捕获到其他类型的异常 e，表示数据库操作出现问题
    {
        STORAGE_ROCKSDB_LOG(ERROR) << LOG_DESC("Query rocksdb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(e);
    }
    //最后，根据上述处理结果，可能返回一个包含选定数据条目的 Entries::Ptr 实例，或者返回一个空指针。
    return Entries::Ptr();
}

size_t RocksDBStorage::commit(int64_t num, const vector<TableData::Ptr>& datas)
{
    try
    {
        auto start_time = utcTime();//首先，记录函数执行的开始时间 start_time，以便计算执行时间

        WriteBatch batch;//创建一个名为 batch 的 WriteBatch 实例，用于批量写入数据。
        tbb::parallel_for(tbb::blocked_range<size_t>(0, datas.size()),//使用 tbb::parallel_for 并行地遍历传入的 datas 向量中的每个 TableData::Ptr 对象。
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                {       //对于每个 TableData::Ptr 对象，首先创建一个 key2value 的指针，指向一个空的 map<string, vector<map<string, string>>>。
                    shared_ptr<map<string, vector<map<string, string>>>> key2value =
                        make_shared<map<string, vector<map<string, string>>>>();

                    auto tableInfo = datas[i]->info;

                    if (m_shouldCompleteDirty)
                    {   //根据 m_shouldCompleteDirty 的值，调用不同的函数来处理脏数据或新数据条目，并将结果存储在 key2value 中。
                        processEntries(num, key2value, tableInfo, datas[i]->dirtyEntries, true);
                    }
                    else
                    {
                        processDirtyEntries(num, key2value, tableInfo, datas[i]->dirtyEntries);
                    }
                    processEntries(num, key2value, tableInfo, datas[i]->newEntries, false);
                    //遍历 key2value，将数据序列化并使用 m_db->PutWithLock 将数据添加到 batch 中。
                    for (const auto& it : *key2value)
                    {
                        string entryKey = tableInfo->name + "_" + it.first;
                        stringstream ss;
                        boost::archive::binary_oarchive oa(ss);
                        oa << it.second;
                        {
                            m_db->PutWithLock(batch, entryKey, ss.str(), m_writeBatchMutex);
                        }
                    }
                }
            });
        auto encode_time_cost = utcTime();//使用 utcTime 函数记录编码和写入数据库的时间开销，以备后续日志记录。

        WriteOptions options;//创建一个名为 options 的 WriteOptions 实例，用于控制写入操作的选项。
        // by default sync is false, if true, must enable WAL
        options.sync = false;
        // by default WAL is enable
        options.disableWAL = m_disableWAL;//根据默认设置，options.sync 为 false，表示不同步写入；options.disableWAL 取决于 m_disableWAL 的设置。
        //使用 m_db->Write 方法将 batch 中的数据写入 RocksDB 数据库。
        m_db->Write(options, batch);
        auto writeDB_time_cost = utcTime();
        STORAGE_ROCKSDB_LOG(DEBUG)//在写入完成后，使用日志记录函数 STORAGE_ROCKSDB_LOG 输出调试信息。
            << LOG_BADGE("Commit") << LOG_DESC("Write to db")
            << LOG_KV("encodeTimeCost", encode_time_cost - start_time)
            << LOG_KV("writeDBTimeCost", writeDB_time_cost - encode_time_cost)
            << LOG_KV("totalTimeCost", utcTime() - start_time);

        return datas.size();
    }

    /*与前面解释的类似，代码中有异常捕获块来处理可能出现的 DatabaseNeedRetry 和其他异常。
DatabaseNeedRetry 异常会记录警告信息，其他异常会记录错误信息，并将异常继续抛出。*/
    catch (DatabaseNeedRetry const& e)
    {
        STORAGE_ROCKSDB_LOG(WARNING) << LOG_DESC("Query rocksdb exception, need to retry again ")
                                     << LOG_KV("msg", boost::diagnostic_information(e));
    }
    catch (exception& e)
    {
        STORAGE_ROCKSDB_LOG(ERROR) << LOG_DESC("Commit rocksdb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return 0;
}

void RocksDBStorage::processEntries(int64_t num,
    shared_ptr<map<string, vector<map<string, string>>>> key2value, TableInfo::Ptr tableInfo,
    Entries::Ptr entries, bool isDirtyEntries)//函数的主要作用是将传入的数据条目根据不同情况添加到 key2value 映射中，以便后续将数据写入数据库。
{       
    for (size_t j = 0; j < entries->size(); ++j)//函数使用 for 循环遍历传入的 entries 向量中的每个数据条目
    {
        auto entry = entries->get(j);
        auto key = entry->getField(tableInfo->key);

        auto it = map<string, vector<map<string, string>>>::iterator();
        if (!isDirtyEntries && entry->force())
        {  // only new entries can be forced
            it = key2value->insert(make_pair(key, vector<map<string, string>>())).first;
        }
        else
        {
            it = key2value->find(key);
            if (it == key2value->end())
            {      //对于每个数据条目，首先获取其键 key，该键是根据传入的 tableInfo 和数据条目中的字段计算得到的。
                string entryKey = tableInfo->name;
                entryKey.append("_").append(key);

                string value;

                // this exception has already been catched in commit function
                auto s = m_db->Get(ReadOptions(), entryKey, value);
                if (!s.ok() && !s.IsNotFound())
                {
                    STORAGE_ROCKSDB_LOG(ERROR)
                        << LOG_DESC("Query rocksdb failed") << LOG_KV("status", s.ToString());

                    BOOST_THROW_EXCEPTION(
                        StorageException(-1, "Query rocksdb exception:" + s.ToString()));
                }
                if (s.IsNotFound())
                {
                    it = key2value->insert(make_pair(key, vector<map<string, string>>())).first;
                }
                else
                {
                    vector<map<string, string>> res;
                    stringstream ss(value);
                    boost::archive::binary_iarchive ia(ss);
                    ia >> res;
                    it = key2value->emplace(key, move(res)).first;
                }
            }
        }

        auto copyFromEntry = [num](map<string, string>& value, Entry::Ptr entry) {
            for (const auto& fieldIt : *(entry))
            {
                value[fieldIt.first] = fieldIt.second;
            }
            value[NUM_FIELD] = boost::lexical_cast<string>(num);
            value[ID_FIELD] = boost::lexical_cast<string>(entry->getID());
            value[STATUS] = boost::lexical_cast<string>(entry->getStatus());
        };
        if (isDirtyEntries)//根据传入的 isDirtyEntries 参数，决定是否处理脏数据条目（已存在的数据）或新数据条目。
        {   //如果 isDirtyEntries 为 false，并且数据条目需要强制添加（通过 entry->force() 判断），则表示这是新数据条目，将其添加到 key2value 映射中。
            //否则，表示这是脏数据条目，需要查找现有的数据条目，以便进行更新。
            // binary search
            map<string, string> fakeEntry{{ID_FIELD, to_string(entry->getID())}};
            auto originEntryIterator = lower_bound(it->second.begin(), it->second.end(), fakeEntry,
                [](const map<string, string>& lhs, const map<string, string>& rhs) {
                    return boost::lexical_cast<uint64_t>(lhs.at(ID_FIELD)) <
                           boost::lexical_cast<uint64_t>(rhs.at(ID_FIELD));
                });
            if (originEntryIterator == it->second.end() ||
                fakeEntry.at(ID_FIELD) != originEntryIterator->at(ID_FIELD))//首先，构造一个名为 fakeEntry 的 map，其中只包含数据条目的 ID 字段，用于进行二进制搜索。
            {
                STORAGE_ROCKSDB_LOG(FATAL)
                    << "cannot find dirty entry" << LOG_KV("id", fakeEntry.at(ID_FIELD));
            }
            copyFromEntry(*originEntryIterator, entry);
        }
        else
        {  // new entry
        //最后，将 value 添加到对应的键（即数据条目的键）在 key2value 映射中的向量中。
        //将额外的元数据，如 NUM_FIELD、ID_FIELD 和 STATUS，添加到 value 中。
            map<string, string> value;
            copyFromEntry(value, entry);
            it->second.push_back(move(value));
        }
    }
}

void RocksDBStorage::processDirtyEntries(int64_t num,
    shared_ptr<map<string, vector<map<string, string>>>> key2value, TableInfo::Ptr tableInfo,
    Entries::Ptr entries)
{
    for (size_t j = 0; j < entries->size(); ++j)//函数使用 for 循环遍历传入的 entries 向量中的每个数据条目。
    {
        auto entry = entries->get(j);
        if (entry->deleted())//对于每个数据条目，首先检查是否已被标记为删除（通过 entry->deleted() 判断），如果是，则跳过处理。
        {
            continue;
        }
        auto key = entry->getField(tableInfo->key);//否则，获取数据条目的键 key，该键是根据传入的 tableInfo 和数据条目中的字段计算得到的。

        auto it = key2value->find(key);//使用 key 在 key2value 映射中查找对应的向量，如果不存在，则在映射中插入一个新的键值对，以 key 为键，对应一个空的向量。
        if (it == key2value->end())
        {
            it = key2value->insert(make_pair(key, vector<map<string, string>>())).first;
        }

        map<string, string> value;//创建一个名为 value 的 map，将数据条目的字段和值复制到其中。
        for (auto& fieldIt : *(entry))
        {
            value[fieldIt.first] = fieldIt.second;
        }//将额外的元数据，如 NUM_FIELD、ID_FIELD 和 STATUS，添加到 value 中。
        value[NUM_FIELD] = boost::lexical_cast<string>(num);
        value[ID_FIELD] = boost::lexical_cast<string>(entry->getID());
        value[STATUS] = boost::lexical_cast<string>(entry->getStatus());
        it->second.push_back(value);//最后，将 value 添加到 key2value 映射中对应的向量中。
    }
}
