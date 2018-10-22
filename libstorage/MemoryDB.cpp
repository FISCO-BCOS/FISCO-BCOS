#include "MemoryDB.h"

#include <json/json.h>
#include <libdevcore/Hash.h>
#include <libdevcore/easylog.h>
#include "Common.h"
#include "DB.h"

using namespace dev;
using namespace dev::storage;

void dev::storage::MemoryDB::init(const std::string &tableName)
{
  LOG(DEBUG) << "Init MemoryDB:" << tableName;

  // _tableInfo = _remoteDB->info(tableName);
}

Entries::Ptr dev::storage::MemoryDB::select(const std::string &key,
                                            Condition::Ptr condition)
{
  try
  {
    LOG(DEBUG) << "MemoryDB query data: " << key;

    Entries::Ptr entries = std::make_shared<Entries>();

    auto it = _cache.find(key);
    if (it == _cache.end())
    {
      if (_remoteDB.get() != NULL)
      {
        entries =
            _remoteDB->select(_blockHash, _blockNum, _tableInfo->name, key);

        LOG(DEBUG) << "AMOPDB get :" << entries->size() << " items";

        _cache.insert(std::make_pair(key, entries));
      }
    }
    else
    {
      entries = it->second;
    }

    if (entries.get() == NULL)
    {
      LOG(ERROR) << "No data found";
      return entries;
    }

    return processEntries(entries, condition);
  }
  catch (std::exception &e)
  {
    LOG(ERROR) << "MemoryDB error:" << e.what();
  }

  return Entries::Ptr();
}

size_t dev::storage::MemoryDB::update(const std::string &key,
                                      Entry::Ptr entry,
                                      Condition::Ptr condition)
{
  try
  {
    LOG(DEBUG) << "MemoryDB update data: " << key;

    Entries::Ptr entries = std::make_shared<Entries>();

    auto it = _cache.find(key);
    if (it == _cache.end())
    {
      if (_remoteDB.get() != NULL)
      {
        entries =
            _remoteDB->select(_blockHash, _blockNum, _tableInfo->name, key);

        LOG(DEBUG) << "AMOPDB get:" << entries->size() << " items";

        _cache.insert(std::make_pair(key, entries));
      }
    }
    else
    {
      entries = it->second;
    }

    if (entries.get() == NULL)
    {
      LOG(ERROR) << "No data found";

      return 0;
    }
    checkFiled(entry);
    Entries::Ptr updateEntries = processEntries(entries, condition);
    for (size_t i = 0; i < updateEntries->size(); ++i)
    {
      Entry::Ptr updateEntry = updateEntries->get(i);

      for (auto &it : *(entry->fields()))
      {
        updateEntry->setField(it.first, it.second);
      }
    }

    entries->setDirty(true);

    return updateEntries->size();
  }
  catch (std::exception &e)
  {
    LOG(ERROR) << "Request DB error:" << e.what();
  }

  return 0;
}

size_t dev::storage::MemoryDB::insert(const std::string &key,
                                      Entry::Ptr entry)
{
  try
  {
    LOG(DEBUG) << "MemoryDB insert data: " << key;

    Entries::Ptr entries = std::make_shared<Entries>();
    Condition::Ptr condition = std::make_shared<Condition>();

    auto it = _cache.find(key);
    if (it == _cache.end())
    {
      if (_remoteDB.get() != NULL)
      {
        entries =
            _remoteDB->select(_blockHash, _blockNum, _tableInfo->name, key);

        LOG(DEBUG) << "AMOPDB get :" << entries->size() << " items.";

        _cache.insert(std::make_pair(key, entries));
      }
    }
    else
    {
      entries = it->second;
    }
    
    checkFiled(entry);
    if (entries->size() == 0)
    {
      entries->addEntry(entry);

      _cache.insert(std::make_pair(key, entries));

      return 1;
    }
    else
    {
      entries->addEntry(entry);

      return 1;
    }
  }
  catch (std::exception &e)
  {
    LOG(ERROR) << "MemoryDB error";
  }

  return 1;
}

size_t dev::storage::MemoryDB::remove(const std::string &key,
                                      Condition::Ptr condition)
{
  LOG(DEBUG) << "MemoryDB remove data : " << key;

  Entries::Ptr entries = std::make_shared<Entries>();

  auto it = _cache.find(key);
  if (it == _cache.end())
  {
    if (_remoteDB.get() != NULL)
    {
      entries = _remoteDB->select(_blockHash, _blockNum, _tableInfo->name, key);

      LOG(DEBUG) << "AMOPDB get :" << entries->size() << " items.";

      _cache.insert(std::make_pair(key, entries));
    }
  }
  else
  {
    entries = it->second;
  }

  Entries::Ptr updateEntries = processEntries(entries, condition);
  for (size_t i = 0; i < updateEntries->size(); ++i)
  {
    Entry::Ptr removeEntry = updateEntries->get(i);

    removeEntry->setStatus(1);
  }

  entries->setDirty(true);

  return updateEntries->size();
}

h256 dev::storage::MemoryDB::hash()
{
  bytes data;
  for (auto it : _cache)
  {
    if (it.second->dirty())
    {
      data.insert(data.end(), it.first.begin(), it.first.end());
      for (size_t i = 0; i < it.second->size(); ++i)
      {
        if (it.second->get(i)->dirty())
        {
          for (auto fieldIt : *(it.second->get(i)->fields()))
          {
            if (isHash(fieldIt.first))
            {
              data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());
              data.insert(data.end(), fieldIt.second.begin(), fieldIt.second.end());
            }
          }
        }
      }
    }
  }

  if (data.empty())
  {
    return h256();
  }

  std::string str(data.begin(), data.end());

  bytesConstRef bR(data.data(), data.size());
  h256 hash = dev::sha256(bR);

  return hash;
}

bool dev::storage::MemoryDB::isHash(std::string _key)
{
  if (!_key.empty())
  {
    return ((_key.substr(0, 1) != "_" && _key.substr(_key.size() - 1, 1) != "_") || (_key == STORAGE_STATUS));
  }
  LOG(ERROR) << "Empty key error.";
  return false;
  
}

void dev::storage::MemoryDB::clear() { _cache.clear(); }

std::map<std::string, Entries::Ptr> *dev::storage::MemoryDB::data()
{
  return &_cache;
}

void dev::storage::MemoryDB::setStateStorage(Storage::Ptr amopDB)
{
  _remoteDB = amopDB;
}

Entries::Ptr MemoryDB::processEntries(Entries::Ptr entries,
                                      Condition::Ptr condition)
{
  if (condition->getConditions()->empty())
  {
    return entries;
  }

  Entries::Ptr result = std::make_shared<Entries>();
  for (size_t i = 0; i < entries->size(); ++i)
  {
    Entry::Ptr entry = entries->get(i);
    if (processCondition(entry, condition))
    {
      result->addEntry(entry);
    }
  }

  return result;
}

bool dev::storage::MemoryDB::processCondition(Entry::Ptr entry,
                                              Condition::Ptr condition)
{
  try
  {
    for (auto it : *condition->getConditions())
    {
      if (entry->getStatus() == Entry::Status::DELETED)
      {
        return false;
      }

      std::string lhs = entry->getField(it.first);
      std::string rhs = it.second.second;

      if (it.second.first == Condition::Op::eq)
      {
        if (lhs != rhs)
        {
          return false;
        }
      }
      else if (it.second.first == Condition::Op::ne)
      {
        if (lhs == rhs)
        {
          return false;
        }
      }
      else
      {
        if (lhs.empty())
        {
          lhs = "0";
        }
        if (rhs.empty())
        {
          rhs = "0";
        }

        int lhsNum = boost::lexical_cast<int>(lhs);
        int rhsNum = boost::lexical_cast<int>(rhs);

        switch (it.second.first)
        {
        case Condition::Op::eq:
        case Condition::Op::ne:
        {
          break;
        }
        case Condition::Op::gt:
        {
          if (lhsNum <= rhsNum)
          {
            return false;
          }
          break;
        }
        case Condition::Op::ge:
        {
          if (lhsNum < rhsNum)
          {
            return false;
          }
          break;
        }
        case Condition::Op::lt:
        {
          if (lhsNum >= rhsNum)
          {
            return false;
          }
          break;
        }
        case Condition::Op::le:
        {
          if (lhsNum > rhsNum)
          {
            return false;
          }
          break;
        }
        }
      }
    }
  }
  catch (std::exception &e)
  {
    LOG(ERROR) << "Compare error:" << e.what();

    return false;
  }

  return true;
}

void MemoryDB::setBlockHash(h256 blockHash) { _blockHash = blockHash; }

void MemoryDB::setBlockNum(int blockNum) { _blockNum = blockNum; }

void MemoryDB::setTableInfo(TableInfo::Ptr tableInfo) { _tableInfo = tableInfo; }

void MemoryDB::checkFiled(Entry::Ptr entry)
{
  for (auto &it : *(entry->fields()))
  {
    if (_tableInfo->fields.end() == find(_tableInfo->fields.begin(), _tableInfo->fields.end(), it.first))
    {
      LOG(ERROR) << "table:" << _tableInfo->name << " doesn't have field:" << it.first;
      throw std::invalid_argument(std::string("table:") + _tableInfo->name + " doesn't have field:" + it.first);
    }
  }
}
