#pragma once

#include <libweb3jsonrpc/ChannelRPCServer.h>
#include "StateDB.h"
#include "StateStorage.h"

namespace dev {

namespace storage {

class MemoryStateDBFactory : public StateDBFactory {
 public:
  typedef std::shared_ptr<MemoryStateDBFactory> Ptr;

  virtual ~MemoryStateDBFactory() {}

  StateDB::Ptr openTable(h256 blockHash, int num,
                         const std::string &table) override;
  StateDB::Ptr createTable(h256 blockHash, int num,
                           const std::string &tableName,
                           const std::string &keyField,
                           const std::vector<std::string> &valueField) override;

  virtual StateStorage::Ptr stateStorage() { return _stateStorage; }
  virtual void setStateStorage(StateStorage::Ptr stateStorage) {
    _stateStorage = stateStorage;
  }

  void setBlockHash(h256 blockHash);
  void setBlockNum(int blockNum);

 private:
  StateStorage::Ptr _stateStorage;
  h256 _blockHash;
  int _blockNum;
};

}  // namespace storage

}  // namespace dev
