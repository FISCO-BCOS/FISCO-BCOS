#pragma once

#include <libdevcore/Common.h>
#include <libprecompiled/PrecompiledContext.h>
#include "DB.h"

namespace dev {

namespace precompiled {

#if 0
contract DB {
    function select(string, Condition) public constant returns(Entries);
    function insert(string, Entry) public returns(int);
    function update(string, Entry, Condition) public returns(int);
    function remove(string, Condition) public returns(int);
    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}
{
    "31afac36": "insert(string,address)",
    "7857d7c9": "newCondition()",
    "13db9346": "newEntry()",
    "28bb2117": "remove(string,address)",
    "e8434e39": "select(string,address)",
    "bf2b70a1": "update(string,address,address)"
}
#endif

class DBPrecompiled : public Precompiled {
 public:
  typedef std::shared_ptr<DBPrecompiled> Ptr;

  virtual ~DBPrecompiled(){};

  virtual void beforeBlock(std::shared_ptr<PrecompiledContext>) override;
  virtual void afterBlock(std::shared_ptr<PrecompiledContext>,
                          bool commit) override;

  virtual std::string toString(std::shared_ptr<PrecompiledContext>) override;

  virtual bytes call(std::shared_ptr<PrecompiledContext> context,
                     bytesConstRef param) override;

  dev::storage::DB::Ptr getDB() { return _DB; }
  void setDB(dev::storage::DB::Ptr DB) { _DB = DB; }

  h256 hash();

 private:
  dev::storage::DB::Ptr _DB;
};

}  // namespace precompiled

}  // namespace dev
