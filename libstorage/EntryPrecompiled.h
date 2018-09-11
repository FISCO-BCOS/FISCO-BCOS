#pragma once

#include "../libprecompiled/PrecompiledContext.h"
#include "DB.h"

namespace dev {

namespace precompiled {

#if 0
contract Entry {
    function getInt(string) public constant returns(int);
    function getString(string) public constant returns(String);

    function set(string, int) public;
    function set(string, string) public;
}
{
    "fda69fae": "getInt(string)",
    "2ef8ba74": "set(string,int256)",
    "e942b516": "set(string,string)",
    "d52decd4": "getBytes64(string)",
    "bf40fac1": "getAddress(string)"
}
#endif

class EntryPrecompiled : public Precompiled {
 public:
  typedef std::shared_ptr<EntryPrecompiled> Ptr;

  virtual ~EntryPrecompiled(){};

  virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
  virtual void afterBlock(std::shared_ptr<PrecompiledContext>, bool commit);

  virtual std::string toString(std::shared_ptr<PrecompiledContext>);

  virtual bytes call(std::shared_ptr<PrecompiledContext> context,
                     bytesConstRef param);

  void setEntry(dev::storage::Entry::Ptr entry) { _entry = entry; }
  dev::storage::Entry::Ptr getEntry() { return _entry; }

 private:
  dev::storage::Entry::Ptr _entry;
};

}  // namespace precompiled

}  // namespace dev
