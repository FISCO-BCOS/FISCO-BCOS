#include "storage/LRUStorage.h"
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

namespace std
{
inline ostream& operator<<(ostream& os, const std::optional<bcos::storage::Entry>& entry)
{
    os << entry.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::optional<bcos::storage::Table>& table)
{
    os << table.has_value();
    return os;
}

inline ostream& operator<<(ostream& os, const std::unique_ptr<bcos::Error>& error)
{
    os << error->what();
    return os;
}

inline ostream& operator<<(ostream& os, const std::tuple<std::string, bcos::crypto::HashType>& pair)
{
    os << std::get<0>(pair) << " " << std::get<1>(pair).hex();
    return os;
}
}  // namespace std

namespace bcos::test
{
using namespace bcos::storage;

class LRUStorageFixture
{
public:
    LRUStorageFixture()
    {
        memoryStorage = std::make_shared<storage::StateStorage>(nullptr);
        BOOST_TEST(memoryStorage != nullptr);
        tableFactory = std::make_shared<executor::LRUStorage>(memoryStorage);
        hashImpl = std::make_shared<Keccak256Hash>();

        BOOST_TEST(tableFactory != nullptr);
    }

    ~LRUStorageFixture() {}

    std::shared_ptr<crypto::Hash> hashImpl;
    std::shared_ptr<storage::StateStorage> memoryStorage;
    protocol::BlockNumber m_blockNumber = 0;
    std::shared_ptr<executor::LRUStorage> tableFactory;
};

BOOST_FIXTURE_TEST_SUITE(TestLRUStorage, LRUStorageFixture)

BOOST_AUTO_TEST_CASE(capacity)
{
    tableFactory->setMaxCapacity(100);
    tableFactory->asyncCreateTable("table", "value",
        [](Error::UniquePtr error, std::optional<Table>) { BOOST_CHECK(!error); });

    for (size_t i = 0; i < 10 * 100; ++i)
    {
        std::string key = "key" + boost::lexical_cast<std::string>(i);

        Entry data;
        data.importFields({"hello world, hello world, hello world, hello world!"});

        tableFactory->asyncSetRow(
            "table", key, std::move(data), [](Error::UniquePtr error) { BOOST_CHECK(!error); });
    }

    BOOST_CHECK_LT(tableFactory->capacity(), 100);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test