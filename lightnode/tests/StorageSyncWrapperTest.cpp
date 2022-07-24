#include <bcos-lightnode/storage/StorageSyncWrapper.h>
#include <bcos-table/src/StateStorage.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::storage;

struct StorageSyncWrapperFixture
{
    StorageSyncWrapperFixture() : storage(std::make_shared<StateStorage>(nullptr)) {}

    StorageSyncWrapper<StateStorage::Ptr> storage;
};

BOOST_FIXTURE_TEST_SUITE(StorageSyncWrapperTest, StorageSyncWrapperFixture)

BOOST_AUTO_TEST_CASE(getRow)
{
    Entry entry;
    entry.importFields({"Hello world!"});
    storage.setRow("table", "key", std::move(entry));

    auto gotEntry = storage.getRow("table", "key");
    auto field = gotEntry->get();

    BOOST_CHECK_EQUAL(field, "Hello world!");
}

BOOST_AUTO_TEST_SUITE_END()