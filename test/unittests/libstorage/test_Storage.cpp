/*
 * test_Storage.cpp
 *
 *  Created on: 2018年4月28日
 *      Author: mo nan
 */

#include "Common.h"
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_Storage
{
struct StorageFixture
{
    StorageFixture()
    {
        entry = std::make_shared<Entry>();
        entries = std::make_shared<Entries>();
        condition = std::make_shared<Condition>();
    }

    ~StorageFixture() {}

    dev::storage::Entry::Ptr entry;
    dev::storage::Entries::Ptr entries;
    dev::storage::Condition::Ptr condition;
    dev::storage::Table::Ptr stateDB;
};

BOOST_FIXTURE_TEST_SUITE(Storage, StorageFixture)

BOOST_AUTO_TEST_CASE(entryTest)
{
    entry->setStatus(1);
    BOOST_TEST_TRUE(entry->getStatus() == 1u);

    BOOST_TEST_TRUE(entry->dirty() == true);

    entry->setField("key", "value");
    BOOST_TEST_TRUE(entry->getField("key") == "value");

    BOOST_TEST_TRUE(entry->dirty() == true);


    entry->setDirty(false);
    BOOST_TEST_TRUE(entry->dirty() == false);
}

BOOST_AUTO_TEST_CASE(entriesTest)
{
    BOOST_TEST_TRUE(entries->size() == 0u);
    BOOST_TEST_TRUE(entries->dirty() == false);

    entries->addEntry(entry);

    BOOST_TEST_TRUE(entries->size() == 1u);
    BOOST_TEST_TRUE(entries->get(0) == entry);
    BOOST_CHECK_THROW(entries->get(5), dev::storage::StorageException);
    BOOST_TEST_TRUE(entries->dirty() == true);
}

BOOST_AUTO_TEST_CASE(conditionTest)
{
    condition->limit(1);
    BOOST_TEST_TRUE(condition->getConditions()->empty());
}

BOOST_AUTO_TEST_CASE(StateExceptionTest)
{
    StorageException storageException{1, "error"};
    BOOST_TEST_TRUE(storageException.errorCode() == 1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_Storage
