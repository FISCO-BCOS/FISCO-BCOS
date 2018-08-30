#pragma once

#include <json/json.h>
#include <leveldb/db.h>
#include <libdevcore/FixedHash.h>
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include "StateDB.h"
#include "StateStorage.h"

namespace dev {
 
namespace storage {

class LevelDBStateStorage : public StateStorage {
 public:
  typedef std::shared_ptr<LevelDBStateStorage> Ptr;

  virtual ~LevelDBStateStorage(){};

  virtual TableInfo::Ptr info(const std::string &table) override;
  virtual Entries::Ptr select(h256 hash, int num, const std::string &table,
                              const std::string &key) override;
  virtual size_t commit(h256 hash, int num,
                        const std::vector<TableData::Ptr> &datas,
                        h256 blockHash) override;
  virtual bool onlyDirty() override;

  void setDB(std::shared_ptr<leveldb::DB> db);

 private:
  std::shared_ptr<leveldb::DB> _db;
};

}  // namespace storage

}  // namespace dev
