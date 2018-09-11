#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libprecompiled/StringFactoryPrecompiled.h>
#include <libstorage/EntryPrecompiled.h>
#include <libstorage/DB.h>
#include <boost/test/unit_test.hpp>
#include "unittest/Common.h"

using namespace dev;
using namespace dev::storage;
using namespace dev::precompiled;
using namespace dev::eth;

namespace test_precompiled {

struct EntryPrecompiledFixture {
  EntryPrecompiledFixture() {
    entry = std::make_shared<Entry>();
    stringFactoryPrecompiled = std::make_shared<StringFactoryPrecompiled>();
    precompiledContext =
        std::make_shared<dev::precompiled::PrecompiledContext>();
    entryPrecompiled = std::make_shared<dev::precompiled::EntryPrecompiled>();

    entryPrecompiled->setEntry(entry);
    entryPrecompiled->setStringFactoryPrecompiled(stringFactoryPrecompiled);
  }
  ~EntryPrecompiledFixture() {}

  dev::storage::Entry::Ptr entry;
  StringFactoryPrecompiled::Ptr stringFactoryPrecompiled;
  dev::precompiled::PrecompiledContext::Ptr precompiledContext;
  dev::precompiled::EntryPrecompiled::Ptr entryPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(EntryPrecompiled, EntryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testBeforeAndAfterBlock) {
  entryPrecompiled->beforeBlock(precompiledContext);
  entryPrecompiled->afterBlock(precompiledContext, true);
  BOOST_TEST(entryPrecompiled->toString(precompiledContext) == "Entry");
}

BOOST_AUTO_TEST_CASE(testEntry) {
  entry->setField("key", "value");
  entryPrecompiled->setEntry(entry);
  BOOST_TEST(entryPrecompiled->getEntry() == entry);
}

BOOST_AUTO_TEST_CASE(testGetInt) {
  entry->setField("keyInt", "100");
  ContractABI abi;

  bytes bint = abi.abiIn("getInt(string)", "keyInt");
  bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bint));
  u256 num;
  abi.abiOut(bytesConstRef(&out), num);
  BOOST_TEST(num == u256(100));
}

BOOST_AUTO_TEST_CASE(testGetString) {
  entry->setField("keyString", "hello");
  ContractABI abi;

  bytes bstr = abi.abiIn("getString(string)", "keyString");
  bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bstr));
  Address address;
  abi.abiOut(bytesConstRef(&out), address);
  auto stringPrecompiled = precompiledContext->getPrecompiled(address);
  std::string outstr = stringPrecompiled->toString(precompiledContext);
  BOOST_TEST(outstr == "hello");
}

BOOST_AUTO_TEST_CASE(testSetAddress) {
  ContractABI abi;
  Address address =
      stringFactoryPrecompiled->newString(precompiledContext, "world");
  bytes sstr = abi.abiIn("set(string,address)", "keyAddress", address);
  entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));

  entryPrecompiled->getEntry()->getField("keyAddress") == "world";
}

BOOST_AUTO_TEST_CASE(testGetAddress) {
  ContractABI abi;
  entry->setField("keyAddress", "1000");
  bytes gstr = abi.abiIn("getAddress(string)", "keyAddress");
  bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&gstr));
  Address address;
  abi.abiOut(bytesConstRef(&out), address);
  BOOST_TEST(address == Address("1000"));
}

BOOST_AUTO_TEST_CASE(testSetInt) {
  ContractABI abi;
  bytes sstr = abi.abiIn("set(string,int256)", "keyInt", u256(200));
  entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
  BOOST_TEST(entry->getField("keyInt") ==
             boost::lexical_cast<std::string>(200));
}

BOOST_AUTO_TEST_CASE(testSetString) {
  ContractABI abi;
  bytes sstr = abi.abiIn("set(string,string)", "keyString", "you");
  entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
  BOOST_TEST(entry->getField("keyString") == "you");
}

BOOST_AUTO_TEST_CASE(testGetBytes64) {
  entry->setField("keyString", "1000");
  ContractABI abi;
  bytes sstr = abi.abiIn("getBytes64(string)", "keyString");
  bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&sstr));
  string64 retout;
  abi.abiOut(bytesConstRef(&out), retout);
  std::string s = "1000";
  string64 ret;
  for (unsigned i = 0; i < retout.size(); ++i)
        ret[i] = i < s.size() ? s[i] : 0;
  BOOST_TEST(retout == ret);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_precompiled
