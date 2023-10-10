#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include <boost/test/unit_test.hpp>

struct TestHashFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestHash, TestHashFixture)

BOOST_AUTO_TEST_CASE(testTwoHash)
{
    auto version = (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION;
    bcos::storage::StateStorage stateStorage(nullptr);
    bcos::storage::KeyPageStorage keyPageStorage(nullptr, 10240, version);

    for (int i = 0; i < 100; i++)
    {
        std::string key = "key" + std::to_string(i);
        bcos::storage::Entry entry;
        entry.set("value" + std::to_string(i));

        stateStorage.asyncSetRow("test_table", key, entry, [](auto) {});
        keyPageStorage.asyncSetRow("test_table", key, entry, [](auto) {});
    }

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto hash1 = stateStorage.hash(hashImpl, false);
    auto hash2 = keyPageStorage.hash(hashImpl, false);
    BOOST_CHECK_NE(hash1, hash2);

    auto hash3 = stateStorage.hash(hashImpl, true);
    BOOST_CHECK_EQUAL(hash3, hash2);
}

BOOST_AUTO_TEST_SUITE_END()