#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libprecompiled/StringFactoryPrecompiled.h>
#include <libstorage/EntryPrecompiled.h>
#include <libstorage/StateDB.h>
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

BOOST_AUTO_TEST_CASE(testCall) {
  entry->setField("keyInt", "100");
  entry->setField("keyString", "hello");
  ContractABI abi;

  bytes bint = abi.abiIn("getInt(string)", "keyInt");
  bytes out = entryPrecompiled->call(precompiledContext, bytesConstRef(&bint));
  u256 num;
  abi.abiOut(bytesConstRef(&out), num);
  BOOST_TEST(num == u256(100));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_precompiled
