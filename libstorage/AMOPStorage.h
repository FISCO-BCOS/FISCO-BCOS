#pragma once

#include <json/json.h>
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include "DB.h"
#include "Storage.h"

namespace dev {

namespace storage {

class AMOPStorage : public Storage {
 public:
  typedef std::shared_ptr<AMOPStorage> Ptr;

  AMOPStorage();
  virtual ~AMOPStorage(){};

  virtual TableInfo::Ptr info(const std::string &table) override;
  virtual Entries::Ptr select(h256 hash, int num, const std::string &table,
                              const std::string &key) override;
  virtual size_t commit(h256 hash, int num,
                        const std::vector<TableData::Ptr> &datas,
                        h256 blockHash) override;
  virtual bool onlyDirty() override;

  virtual void setTopic(const std::string &topic);
  // virtual void setBlockHash(h256 blockHash);
  // virtual void setNum(int num);
  virtual void setChannelRPCServer(dev::ChannelRPCServer::Ptr channelRPCServer);
  virtual void setMaxRetry(int maxRetry);

  virtual void setFatalHandler(
      std::function<void(std::exception &)> fatalHandler) {
    _fatalHandler = fatalHandler;
  }

 private:
  Json::Value requestDB(const Json::Value &value);

  std::function<void(std::exception &)> _fatalHandler;

  std::string _topic;
  // h256 _blockHash;
  // int _num = 0;
  dev::ChannelRPCServer::Ptr _channelRPCServer;
  int _maxRetry = 0;
};

}  // namespace storage

}  // namespace dev
