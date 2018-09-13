#include <libdevcore/easylog.h>
#include <boost/algorithm/string.hpp>
#include "MemoryDB.h"
#include "MemoryDBFactory.h"

using namespace dev;
using namespace dev::storage;

DB::Ptr MemoryDBFactory::openTable(h256 blockHash, int num,
                                             const std::string &table) {
  LOG(DEBUG) << "Open table:" << blockHash << " num:" << num
             << " table:" << table;

  MemoryDB::Ptr memoryDB = std::make_shared<MemoryDB>();
  memoryDB->setStateStorage(_stateStorage);
  memoryDB->setBlockHash(_blockHash);
  memoryDB->setBlockNum(_blockNum);

  memoryDB->init(table);

  return memoryDB;
}

DB::Ptr MemoryDBFactory::createTable(
    h256 blockHash, int num, const std::string &tableName,
    const std::string &keyField, const std::vector<std::string> &valueField) {
  LOG(DEBUG) << "Create Table:" << blockHash << " num:" << num
             << " table:" << tableName;

  auto sysTable = openTable(blockHash, num, "_sys_tables_");

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
}

void MemoryDBFactory::setBlockHash(h256 blockHash) {
  _blockHash = blockHash;
}

void MemoryDBFactory::setBlockNum(int blockNum) { _blockNum = blockNum; }
