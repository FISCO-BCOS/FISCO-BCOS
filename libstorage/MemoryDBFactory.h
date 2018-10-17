#pragma once

#include <libweb3jsonrpc/ChannelRPCServer.h>
#include "DB.h"
#include "Storage.h"

namespace dev {

namespace storage {

class MemoryDBFactory : public StateDBFactory {
 public:
  typedef std::shared_ptr<MemoryDBFactory> Ptr;

  MemoryDBFactory();
  virtual ~MemoryDBFactory() {}

  DB::Ptr openTable(h256 blockHash, int num,
                         const std::string &table) override;
  DB::Ptr createTable(h256 blockHash, int num,
                           const std::string &tableName,
                           const std::string &keyField,
                           const std::vector<std::string> &valueField) override;
  virtual Storage::Ptr stateStorage() { return _stateStorage; }
  virtual void setStateStorage(Storage::Ptr stateStorage) {
    _stateStorage = stateStorage;
  }

  void setBlockHash(h256 blockHash);
  void setBlockNum(int blockNum);
 private:
	storage::TableInfo::Ptr getSysTableInfo(const std::string & tableName);
  Storage::Ptr _stateStorage;
  h256 _blockHash;
  int _blockNum;
	std::vector<std::string> m_sysTables;
};

}  // namespace storage

}  // namespace dev
