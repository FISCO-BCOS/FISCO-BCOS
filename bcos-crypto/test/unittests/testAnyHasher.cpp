#include "bcos-crypto/hasher/AnyHasher.h"
#include "bcos-crypto/hasher/OpenSSLHasher.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::crypto::hasher;

struct TestAnyHasher2Fixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestAnyHasher2, TestAnyHasher2Fixture)

BOOST_AUTO_TEST_CASE(testHash)
{
    AnyHasher anyHasher2(openssl::OpenSSL_SHA3_256_Hasher{});

    auto s = sizeof(AnyHasherImpl<openssl::OpenSSL_SHA3_256_Hasher>);
    std::string a = "str123456789012345678901234567890";
    anyHasher2.update(a);
    std::string result1(32, 0);
    anyHasher2.final(result1);

    openssl::OpenSSL_SHA3_256_Hasher hasher;
    hasher.update(a);
    std::string result2(32, 0);
    hasher.final(result2);

    BOOST_CHECK_EQUAL(result1, result2);
}

BOOST_AUTO_TEST_SUITE_END()