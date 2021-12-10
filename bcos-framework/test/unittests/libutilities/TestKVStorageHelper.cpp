#include "../../../libstorage/StateStorage.h"
#include "../../../libutilities/KVStorageHelper.h"
#include "interfaces/crypto/Hash.h"
#include <boost/test/unit_test.hpp>
#include <future>

namespace bcos::test
{
class KVHash : public bcos::crypto::Hash
{
public:
    typedef std::shared_ptr<KVHash> Ptr;

    KVHash() = default;
    virtual ~KVHash(){};
    bcos::crypto::HashType hash(bytesConstRef _data) override
    {
        std::hash<std::string_view> hash;
        return bcos::crypto::HashType(
            hash(std::string_view((const char*)_data.data(), _data.size())));
    }
};

struct TestKVStorageHelperFixture
{
    TestKVStorageHelperFixture()
    {
        auto hash = std::make_shared<KVHash>();
        stateStorage = std::make_shared<bcos::storage::StateStorage>(nullptr);
        kvStorageHelper = std::make_unique<bcos::storage::KVStorageHelper>(stateStorage);
    }

    std::shared_ptr<bcos::storage::StateStorage> stateStorage;
    std::shared_ptr<bcos::storage::KVStorageHelper> kvStorageHelper;
};

BOOST_FIXTURE_TEST_SUITE(TestKVStorageHelper, TestKVStorageHelperFixture)

BOOST_AUTO_TEST_CASE(get)
{
    std::promise<void> getPromise3;
    kvStorageHelper->asyncGet(
        "test_table123", "key3", [&](Error::UniquePtr&& error, std::string_view&& value) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(value, "");

            getPromise3.set_value();
        });
    getPromise3.get_future().get();

    auto table = stateStorage->createTable("test_table", "value1");
    BOOST_CHECK(table);

    bcos::storage::Entry entry1;
    entry1.importFields({"v1"});
    table->setRow("key1", std::move(entry1));

    std::promise<void> getPromise;
    kvStorageHelper->asyncGet(
        "test_table", "key1", [&](Error::UniquePtr&& error, std::string_view&& value) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(value, "v1");

            getPromise.set_value();
        });
    getPromise.get_future().get();

    std::promise<void> getPromise2;
    kvStorageHelper->asyncGet(
        "test_table", "key2", [&](Error::UniquePtr&& error, std::string_view&& value) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(value, "");

            getPromise2.set_value();
        });
    getPromise2.get_future().get();
}

BOOST_AUTO_TEST_CASE(set)
{
    auto table = stateStorage->createTable("test_table", "value1");
    BOOST_CHECK(table);

    std::promise<void> setPromise;
    kvStorageHelper->asyncPut(
        "test_table", "key_from_kv", "hello world!", [&](Error::UniquePtr&& error) {
            BOOST_CHECK(!error);
            setPromise.set_value();
        });

    auto entry = table->getRow("key_from_kv");
    BOOST_CHECK(entry);
    BOOST_CHECK_EQUAL(entry->getField(0), "hello world!");
}

BOOST_AUTO_TEST_CASE(getBatch)
{
    auto keys = std::make_shared<std::vector<std::string>>();
    keys->push_back("1");
    keys->push_back("2");
    keys->push_back("3");

    std::promise<void> batchPromise;
    kvStorageHelper->asyncGetBatch("test_table", keys,
        [&](Error::UniquePtr&& error, std::shared_ptr<std::vector<std::string>>&& values) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(values->size(), 3);

            for (auto& it : *values)
            {
                BOOST_CHECK_EQUAL(it, "");
            }

            batchPromise.set_value();
        });

    batchPromise.get_future().get();

    auto table = stateStorage->createTable("test_table", "value1");
    bcos::storage::Entry entry1;
    entry1.importFields({"value!"});
    table->setRow("1", std::move(entry1));

    bcos::storage::Entry entry2;
    entry2.importFields({"value!"});
    table->setRow("2", std::move(entry2));

    std::promise<void> batchPromise2;
    kvStorageHelper->asyncGetBatch("test_table", keys,
        [&](Error::UniquePtr&& error, std::shared_ptr<std::vector<std::string>>&& values) {
            BOOST_CHECK(!error);
            BOOST_CHECK_EQUAL(values->size(), 3);

            size_t i = 0;
            for (auto& it : *values)
            {
                if (i < 2)
                {
                    BOOST_CHECK_EQUAL(it, "value!");
                }
                else
                {
                    BOOST_CHECK_EQUAL(it, "");
                }

                ++i;
            }

            batchPromise2.set_value();
        });

    batchPromise2.get_future().get();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test