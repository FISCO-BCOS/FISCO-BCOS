#include <libdevcore/easylog.h>
#include <boost/algorithm/string.hpp>
#include "Common.h"
#include "MemoryDB.h"
#include "MemoryDBFactory.h"

using namespace std;
using namespace dev;
using namespace dev::storage;

MemoryDBFactory::MemoryDBFactory()
{
    m_sysTables.push_back(SYSTEM_MINER);
    m_sysTables.push_back(SYSTEM_TABLE);
}

DB::Ptr MemoryDBFactory::openTable(h256 blockHash, int num,
                                             const std::string &tableName) {
  LOG(DEBUG) << "Open table:" << blockHash << " num:" << num
             << " table:" << tableName;
 
 
  auto tableInfo = make_shared<storage::TableInfo>();
  if (m_sysTables.end() != find(m_sysTables.begin(), m_sysTables.end(), tableName))
  {
      tableInfo = getSysTableInfo(tableName);
  }
  else
  {
    auto tempSysTable = openTable(_blockHash, _blockNum, SYSTEM_TABLE);
    auto tableEntries = tempSysTable->select(tableName, tempSysTable->newCondition());
    if (tableEntries->size() == 0u)
    {
      LOG(DEBUG) << tableName << " not exist in _sys_tables_.";
      return nullptr;
    }
    auto entry = tableEntries->get(0);
    tableInfo->name = tableName;
    tableInfo->key = entry->getField("key_field");
    string valueFields = entry->getField("value_field");
    boost::split(tableInfo->fields, valueFields, boost::is_any_of(","));
  }
  tableInfo->fields.emplace_back(storage::STORAGE_STATUS);
  tableInfo->fields.emplace_back("_hash_");
  tableInfo->fields.emplace_back("_num_");
  tableInfo->fields.emplace_back(tableInfo->key);
  MemoryDB::Ptr memoryDB = std::make_shared<MemoryDB>();
  memoryDB->setStateStorage(_stateStorage);
  memoryDB->setBlockHash(_blockHash);
  memoryDB->setBlockNum(_blockNum);
  memoryDB->setTableInfo(tableInfo);
  memoryDB->init(tableName);

  return memoryDB;
}

DB::Ptr MemoryDBFactory::createTable(
    h256 blockHash, int num, const std::string &tableName,
    const std::string &keyField, const std::vector<std::string> &valueField) {
  LOG(DEBUG) << "Create Table:" << blockHash << " num:" << num
             << " table:" << tableName;
#if 0
  // this is wrong. sysTable should be regist into context
  auto sysTable = openTable(blockHash, num, SYSTEM_TABLE);

  //确认表是否存在
  auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
  if (tableEntries->size() == 0) {
    //写入表信息
    auto tableEntry = sysTable->newEntry();
    // tableEntry->setField("name", tableName);
    tableEntry->setField("key_field", keyField);
    tableEntry->setField("value_field", boost::join(valueField, ","));
    sysTable->insert(tableName, tableEntry);
  }

  return openTable(blockHash, num, tableName);
#endif
  return nullptr;
}

storage::TableInfo::Ptr MemoryDBFactory::getSysTableInfo(const std::string &tableName)
{
    auto tableInfo = make_shared<storage::TableInfo>();
    tableInfo->name = tableName;
    if (tableName == SYSTEM_MINER)
    {
        tableInfo->key = "name";
        tableInfo->fields = vector<string>{"type", "node_id", "enable_num"};
    }
    else if (tableName == SYSTEM_TABLE)
    {
        tableInfo->key = "table_name";
        tableInfo->fields = vector<string>{"key_field", "value_field"};
    }
    return tableInfo;
}

void MemoryDBFactory::setBlockHash(h256 blockHash) {
  _blockHash = blockHash;
}

void MemoryDBFactory::setBlockNum(int blockNum) { _blockNum = blockNum; }
