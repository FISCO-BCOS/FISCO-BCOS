#include "GraphKeyLocks.h"
#include "bcos-utilities/Common.h"
#include "mock/MockExecutor.h"
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace bcos::test
{
struct KeyLocksFixture
{
    KeyLocksFixture() {}

    scheduler::GraphKeyLocks keyLocks;
};

BOOST_FIXTURE_TEST_SUITE(TestKeyLocks, KeyLocksFixture)

BOOST_AUTO_TEST_CASE(acquireKeyLock)
{
    // Test same contextID
    std::string to = "contract1";
    std::string key = "key100";
    int64_t contextID = 1000;

    for (int64_t seq = 0; seq < 10; ++seq)
    {
        BOOST_CHECK(keyLocks.acquireKeyLock(to, key, contextID, seq));
    }

    // Test another contextID
    BOOST_CHECK(!keyLocks.acquireKeyLock(to, key, 1001, 0));

    // Release 5 times
    for (int64_t seq = 0; seq < 5; ++seq)
    {
        keyLocks.releaseKeyLocks(contextID, seq);
    }

    // Test another contextID
    BOOST_CHECK(!keyLocks.acquireKeyLock(to, key, 1001, 0));

    // Release all
    for (int64_t seq = 5; seq < 10; ++seq)
    {
        keyLocks.releaseKeyLocks(contextID, seq);
    }

    BOOST_CHECK(keyLocks.acquireKeyLock(to, key, 1001, 0));
}

BOOST_AUTO_TEST_CASE(getKeyLocksNotHoldingByContext)
{
    std::string to = "contract1";
    std::string keyPrefix = "key";

    for (size_t i = 0; i < 100; ++i)
    {
        keyLocks.acquireKeyLock(to, keyPrefix + boost::lexical_cast<std::string>(i), 100, 10);
    }

    for (size_t i = 100; i < 200; ++i)
    {
        keyLocks.acquireKeyLock(to, keyPrefix + boost::lexical_cast<std::string>(i), 101, 20);
    }

    auto keys = keyLocks.getKeyLocksNotHoldingByContext(to, 101);

    BOOST_CHECK_EQUAL(keys.size(), 100);
    std::vector<std::string> matchKeys;
    for (size_t i = 0; i < 100; ++i)
    {
        std::string matchKey = keyPrefix + boost::lexical_cast<std::string>(i);
        matchKeys.emplace_back(std::move(matchKey));
    }
    std::sort(matchKeys.begin(), matchKeys.end());

    BOOST_CHECK_EQUAL_COLLECTIONS(keys.begin(), keys.end(), matchKeys.begin(), matchKeys.end());
}

BOOST_AUTO_TEST_CASE(deadLock)
{
    std::string to = "contract1";
    std::string key1 = "key1";
    std::string key2 = "key2";

    // No dead lock
    BOOST_CHECK(keyLocks.acquireKeyLock(to, key1, 1000, 1));
    BOOST_CHECK(keyLocks.acquireKeyLock(to, key2, 1001, 1));

    BOOST_CHECK(!keyLocks.acquireKeyLock(to, key2, 1000, 2));
    BOOST_CHECK(!keyLocks.acquireKeyLock(to, key2, 1000, 4));

    BOOST_CHECK(!keyLocks.detectDeadLock(1000));
    BOOST_CHECK(!keyLocks.detectDeadLock(1000));
    BOOST_CHECK(!keyLocks.detectDeadLock(1001));

    // Add more duplicate edge
    BOOST_CHECK(keyLocks.acquireKeyLock(to, key1, 1000, 3));
    BOOST_CHECK(keyLocks.acquireKeyLock(to, key2, 1001, 3));

    BOOST_CHECK(!keyLocks.detectDeadLock(1000));
    BOOST_CHECK(!keyLocks.detectDeadLock(1001));

    // Add a dead lock
    BOOST_CHECK(!keyLocks.acquireKeyLock(to, key1, 1001, 2));

    BOOST_CHECK(keyLocks.detectDeadLock(1000));
    BOOST_CHECK(keyLocks.detectDeadLock(1001));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test