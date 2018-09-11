/*
 * test_Storage.cpp
 *
 *  Created on: 2018年4月28日
 *      Author: mo nan
 */

#include <libstorage/DB.h>
#include <libstorage/StorageException.h>
#include <boost/test/unit_test.hpp>
#include <unittest/Common.h>

using namespace dev;
using namespace dev::storage;

namespace test_Storage {

struct StorageFixture {
  StorageFixture() {
    entry = std::make_shared<Entry>();
    entries = std::make_shared<Entries>();
    condition = std::make_shared<Condition>();
  }

  ~StorageFixture() {
    //什么也不做
  }

  dev::storage::Entry::Ptr entry;
  dev::storage::Entries::Ptr entries;
  dev::storage::Condition::Ptr condition;
  dev::storage::DB::Ptr stateDB;
};

BOOST_FIXTURE_TEST_SUITE(Storage, StorageFixture)

BOOST_AUTO_TEST_CASE(entryTest) {
  entry->setStatus(1);
  BOOST_TEST(entry->getStatus() == 1u);

  BOOST_TEST(entry->dirty() == true);

  entry->setField("key", "value");
  BOOST_TEST(entry->getField("key") == "value");

  BOOST_TEST(entry->dirty() == true);

  BOOST_CHECK_THROW(entry->getField("key1"), dev::storage::StorageException);
  // BOOST_TEST(entry->getField("key1") == "");

  entry->setDirty(false);
  BOOST_TEST(entry->dirty() == false);
}

BOOST_AUTO_TEST_CASE(entriesTest) {
  BOOST_TEST(entries->size() == 0u);
  BOOST_TEST(entries->dirty() == false);

  entries->addEntry(entry);

  BOOST_TEST(entries->size() == 1u);
  BOOST_TEST(entries->get(0) == entry);
  BOOST_CHECK_THROW(entries->get(5), dev::storage::StorageException);
  BOOST_TEST(entries->dirty() == true);
}

BOOST_AUTO_TEST_CASE(conditionTest) {
  condition->limit(1);
  BOOST_TEST(condition->getConditions()->empty());
}

BOOST_AUTO_TEST_CASE(StateExceptionTest) {
  StorageException storageException{1, "error"};
  BOOST_TEST(storageException.errorCode() == 1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Storage
