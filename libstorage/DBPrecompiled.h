#pragma once

#include <libdevcore/Common.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libprecompiled/StringFactoryPrecompiled.h>
#include <libprecompiled/StringPrecompiled.h>
#include "StateDB.h"

namespace dev {

namespace precompiled {

#if 0
contract DB {
    function select(String, Condition) public constant returns(Entries);
    function select(string, Condition) public constant returns(Entries);

    function insert(String, Entry) public returns(int);
    function insert(string, Entry) public returns(int);

    function update(String, Entry, Condition) public returns(int);
    function update(string, Entry, Condition) public returns(int);

    function remove(String, Condition) public returns(int);
    function remove(string, Condition) public returns(int);

    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}
{
    "59839041": "select(address,address)",
    "c0a2203e": "insert(address,address)",
    "31afac36": "insert(string,address)",
    "7857d7c9": "newCondition()",
    "13db9346": "newEntry()",
    "7f7c1491": "remove(address,address)",
    "28bb2117": "remove(string,address)",
    "e8434e39": "select(string,address)",
    "1eb42523": "update(address,address,address)",
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

  dev::storage::StateDB::Ptr getDB() { return _DB; }
  void setDB(dev::storage::StateDB::Ptr DB) { _DB = DB; }

  void setStringFactoryPrecompiled(
      StringFactoryPrecompiled::Ptr stringFactoryPrecompiled) {
    _stringFactoryPrecompiled = stringFactoryPrecompiled;
  }

  h256 hash();

 private:
  dev::storage::StateDB::Ptr _DB;
  StringFactoryPrecompiled::Ptr _stringFactoryPrecompiled;
};

}  // namespace precompiled

}  // namespace dev
