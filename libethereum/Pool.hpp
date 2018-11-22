/*
 * This file is part of FISCO BCOS.
 * 
 * FISCO BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * FISCO BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * @file: Pool.hpp
 * @author: jimmyshi
 * @date: 4th May 2018
 */


#ifndef ZKG_POOL_H_
#define ZKG_POOL_H_
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <leveldb/db.h>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <libdevcore/easylog.h>
#include "Client.h"

namespace dev
{
namespace eth
{

/*
 *  
 *  MapPool/LevelDBPool
 *          |----->BasePool 
 *                    |-----> VectorPool：
 *                    |            |-----> cm_pool
 *                    |            |-----> gov_data_pool
 *                    |-----> ExistencePool：                           
 *                                 |-----> cm_root_pool
 *                                 |-----> sn_pool
*/

using poolBlockNumber_t = unsigned;
class LevelDBPool
{
  private:
    std::string db_full_name;
    leveldb::DB *db_handler;
    size_t pool_size = 0;
    boost::mutex pool_size_lock;

  public:
    LevelDBPool(const std::string &name)
    {
        string path = getDataDir() + string("pool");
        boost::filesystem::create_directories(path);
        DEV_IGNORE_EXCEPTIONS(boost::filesystem::permissions(path, boost::filesystem::owner_all));

        db_full_name = path + string("/") + name;

        leveldb::Options o;
        o.max_open_files = 256;
        o.create_if_missing = true;
        //leveldb::DB * db = nullptr;
        if (dev::g_withExisting == WithExisting::Rescue) {
            ldb::Status stateStatus = leveldb::RepairDB(db_full_name, o);
            LOG(INFO) << "repair LevelDBPool leveldb:" << stateStatus.ToString();
        }
        // state db
        leveldb::Status s = leveldb::DB::Open(o, db_full_name, &db_handler);
        if (!s.ok() || !db_handler)
        {
            if (boost::filesystem::space(db_full_name).available < 1024)
            {
                LOG(ERROR) << "[LevelDBPool::openDB] Not enough available space found on hard drive.";
                BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
            }
            else
            {
                LOG(ERROR) << "[LevelDBPool::openDB] Not enough available space found on hard drive,status=" << s.ToString()
                             << " ,path=" << db_full_name;

                BOOST_THROW_EXCEPTION(DatabaseAlreadyOpen());
            }
        }

        if (is_exist("pool_size"))
            pool_size = stoi(get("pool_size"));
        LOG(TRACE) << "DB: " << name << " loaded";
    }

    ~LevelDBPool()
    {
        leveldb::Options o;
        leveldb::DestroyDB(db_full_name, o);
        delete db_handler;
        db_handler = NULL;
    }

    void put(const std::string &_key, const std::string &_value)
    {
        leveldb::Status s = db_handler->Put(leveldb::WriteOptions(), _key, _value);

        if (!s.ok())
        {
            LOG(WARNING) << "[LevelDBPool::put] write failed " << s.ToString();
            return;
        }

        pool_size_lock.lock();
        {
            pool_size++;
            leveldb::Status s = db_handler->Put(leveldb::WriteOptions(), "pool_size", to_string(pool_size));
        }
        pool_size_lock.unlock();
    }

    std::string get(const std::string &_key)
    {
        std::string ret;
        leveldb::Status s = db_handler->Get(
            leveldb::ReadOptions(), _key, &ret);
        if (!s.ok())
        {
            LOG(WARNING) << "[LevelDBPool::get] get failed " << s.ToString();
            return std::string();
        }
        return ret;
    }

    bool is_exist(const std::string &_key)
    {
        string t;
        leveldb::Status s = db_handler->Get(
            leveldb::ReadOptions(), _key, &t);
        return s.ok();
    }

    size_t size() { return pool_size; }

    bool empty() { return pool_size == 0; }

};

class MemoryPool : private std::map<std::string, std::string>
{
    //MemoryPool just for testing. Data won't be existed after node restarted.
  private:
    typedef std::map<std::string, std::string> contents;
    typedef std::map<std::string, std::string>::iterator pool_iterator;

  public:
    using contents::begin;
    using contents::empty;
    using contents::end;
    using contents::size;
    //using contents::clear;

    MemoryPool(const string &name){};
    virtual ~MemoryPool(){};

    void put(const std::string &_key, const std::string &_value)
    {
        pool_iterator itr = contents::find(_key); //can not use content::[]
        if (itr == contents::end())
            contents::emplace(_key, _value);
        else
            itr->second = _value;
    }

    std::string get(const std::string &_key)
    {
        pool_iterator itr = contents::find(_key);
        if (itr == contents::end())
        {
            LOG(WARNING) << "MemoryPool key: " << _key << " is not exist";
            return std::string("");
        }
        return itr->second;
    }

    bool is_exist(const std::string &_key)
    {
        pool_iterator itr = contents::find(_key);
        return itr != contents::end();
    }
};

//using BasePool = MemoryPool;
//using BasePool = LevelDBPool;

class BasePool : public LevelDBPool
{
    //BasePool: If query data's blockNumber > current blockNumber(cbn), ignore it
    //cbn: current block number
  private:
    typedef LevelDBPool contents;
    LevelDBPool mask_pool; //Used for ignoring the qurey which blockNumber is bigger than current block number;

  public:
    BasePool(const std::string &name) : contents(name), mask_pool("mask_" + name){};

    void put(poolBlockNumber_t cbn, const std::string &_key, const std::string &_value)
    {
        contents::put(_key, _value);
        LOG(TRACE) << "put key:" << _key << " number:" << cbn;

        if (!mask_pool.is_exist(_key))
        {
            //update pool size
            size_t current_size = current_pool_size(cbn) + 1; 
            mask_pool.put(string("size") + to_string(cbn), to_string(current_size));
        }
        mask_pool.put(_key, std::to_string(cbn));
        mask_pool.put(_value, std::to_string(cbn));
    }

    bool is_exist(poolBlockNumber_t cbn, const std::string &_key)
    {
        return contents::is_exist(_key) && (unsigned)stoi(mask_pool.get(_key)) <= cbn;
    }

    std::string get(poolBlockNumber_t cbn, const std::string &_key)
    {
        if (mask_pool.get(_key) == std::string("") || (unsigned)stoi(mask_pool.get(_key)) > cbn)
            return std::string("");

        return contents::get(_key);
    }

    size_t size(poolBlockNumber_t cbn)
    {
        //LOG(TRACE) << "size current blockNumber " << to_string(cbn);
        return current_pool_size(cbn);
    }

    size_t empty(poolBlockNumber_t cbn)
    {
        return size(cbn) == 0;
    }

    bool is_key_in_block(poolBlockNumber_t cbn, const std::string& _key)
    {
        std::string number_str = mask_pool.get(_key);
        return number_str != "" && cbn == poolBlockNumber_t(stoi(number_str));
    }

private:
    size_t current_pool_size(poolBlockNumber_t blockNumber)
    {
        string size_str = mask_pool.get(string("size") + to_string(blockNumber));
        if (size_str == std::string(""))
        {
            if (blockNumber == 0)
                return 0;
            else
                return current_pool_size(blockNumber - 1);
        }
        else
            return poolBlockNumber_t(stoi(size_str));
    }
};
//*/

class VectorPool : private BasePool
{
    /*
     * Usage eg:
     *  VectorPool vp;
     *  poolBlockNumber_t cbn = env->number(); //current block number
     *  vp.push_back(std::string("aaaa"));
     *  for (size_t i = 0; i < vp.size(); ++i )
     *  {    
     *      std::cout << vp(cbn, i);
     *  }
    */
  private:
    typedef BasePool contents;

  public:
    using contents::empty;
    using contents::size;
    //using contents::clear;

    VectorPool(const std::string &name) : contents(name){};
    virtual ~VectorPool(){};

    void put(poolBlockNumber_t cbn, size_t i, const std::string &_value)
    {
        contents::put(cbn, std::to_string(i), _value);
    }

    std::string get(poolBlockNumber_t cbn, size_t i)
    {
        return contents::get(cbn, std::to_string(i));
    }

    bool is_exist(poolBlockNumber_t cbn, size_t i)
    {
        return contents::is_exist(cbn, std::to_string(i));
    }

    void put_back(poolBlockNumber_t cbn, const std::string &_value)
    {
        contents::put(
            cbn, 
            std::to_string(contents::size(cbn)),
            _value);
    }

    void push_back(poolBlockNumber_t cbn, const std::string &_value) { put_back(cbn, _value); }

    bool put_back_if_not_in_this_block(poolBlockNumber_t cbn, const std::string &_value)
    {
        if (!is_key_in_block(cbn, _value))
        {    
            put_back(cbn, _value);
            return true;
        }
        return false;
    }

    bool push_back_if_not_in_this_block(poolBlockNumber_t cbn, const std::string &_value)
    {
        return put_back_if_not_in_this_block(cbn, _value);
    }

};

class ExistencePool : private BasePool
{
  private:
    typedef BasePool contents;

  public:
    using contents::empty;
    using contents::size;
    //using contents::clear;
    using contents::get;
    using contents::is_exist;
    using contents::put;

    ExistencePool(const std::string &name) : contents(name) {}

    void put(poolBlockNumber_t cbn, const std::string &_value)
    {
        contents::put(
            cbn, 
            _value,
            std::to_string(contents::size(cbn)));
    }

    void put(poolBlockNumber_t cbn, const std::string &_value, size_t id)
    {
        contents::put(
            cbn, 
            _value,
            std::to_string(id));
    }

    bool put_if_not_in_this_block(poolBlockNumber_t cbn, const std::string &_value)
    {
        if (!is_key_in_block(cbn, _value))
        {
            put(cbn, _value);
            return true;
        }
        return false;
    }
};

class CMPool_Singleton
{
  public:
    CMPool_Singleton() {}
    ~CMPool_Singleton() {}

    static CMPool_Singleton &Instance()
    {
        static CMPool_Singleton pool;
        return pool;
    }

    static VectorPool &CMPool_Instance()
    {
        static VectorPool pool("cm");
        return pool;
    }

    static ExistencePool &CMIdPool_Instance()
    {
        static ExistencePool pool("cm_id_pool");
        return pool;
    }

    void put(poolBlockNumber_t cbn, size_t i, const std::string &_value)
    {
        CMIdPool_Instance().put(cbn, _value, i);
        CMPool_Instance().put(cbn, i, _value);
    }

    std::string get(poolBlockNumber_t cbn, size_t i)
    {
        return CMPool_Instance().get(cbn, i);
    }

    size_t get_id(poolBlockNumber_t cbn, const std::string &_value)
    {
        return stoi(CMIdPool_Instance().get(cbn, _value));
    }

    bool is_exist(poolBlockNumber_t cbn, size_t i)
    {
        return CMPool_Instance().is_exist(cbn, i);
    }

    bool is_exist(poolBlockNumber_t cbn, const std::string &_value)
    {
        return CMIdPool_Instance().is_exist(cbn, _value);
    }

    void put_back(poolBlockNumber_t cbn, std::string const &_value)
    {
        size_t id = CMPool_Instance().size(cbn);
        CMPool_Instance().put(
            cbn, 
            id,
            _value);
        CMIdPool_Instance().put(cbn, _value, id);
    }

    void push_back(poolBlockNumber_t cbn, std::string const &_value) { return put_back(cbn, _value); }

    bool put_back_if_not_in_this_block(poolBlockNumber_t cbn, std::string const &_value)
    {
        size_t id = CMPool_Instance().size(cbn);
        if (CMPool_Instance().put_back_if_not_in_this_block(
            cbn, 
            _value)
        )
        {    
            CMIdPool_Instance().put(cbn, _value, id); //id comes from diffrent function, may be some bug here
            return true;
        }

        return false;
    }

    void push_back_if_not_in_this_block(poolBlockNumber_t cbn, std::string const &_value)
    {
        put_back_if_not_in_this_block(cbn, _value);
    }

    size_t size(poolBlockNumber_t cbn)
    {
        return CMPool_Instance().size(cbn);
    }
};

class GovDataPool_Singleton : public VectorPool
{
  public:
    GovDataPool_Singleton(const std::string &name) : VectorPool(name) {}
    static GovDataPool_Singleton &Instance()
    {
        static GovDataPool_Singleton pool("gov");
        return pool;
    }
};
}
}
#endif