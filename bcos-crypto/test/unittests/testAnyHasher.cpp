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

BOOST_AUTO_TEST_CASE(testClone)
{
    AnyHasher hasher(openssl::OpenSSL_SHA3_256_Hasher{});
    auto clone = hasher.clone();

    std::string input = "str123456789012345678901234567890";
    hasher.update(input);
    clone.update(input);

    std::string result1;
    std::string result2;
    hasher.final(result1);
    clone.final(result2);

    BOOST_CHECK_EQUAL(result1, result2);
    BOOST_CHECK_EQUAL(clone.hashSize(), result2.size());
}

BOOST_AUTO_TEST_CASE(testDigest)
{
    AnyHasher hasher(openssl::OpenSSL_Keccak256_Hasher{});
    std::string abc = "abc";
    hasher.update(abc);

    std::string firstDigest;
    hasher.digest(firstDigest);
    std::string repeatedDigest;
    hasher.digest(repeatedDigest);
    BOOST_CHECK_EQUAL(firstDigest, repeatedDigest);

    std::string def = "def";
    hasher.update(def);
    std::string continuedDigest;
    hasher.digest(continuedDigest);

    AnyHasher reference(openssl::OpenSSL_Keccak256_Hasher{});
    std::string abcdef = "abcdef";
    reference.update(abcdef);
    std::string expectedDigest;
    reference.final(expectedDigest);
    BOOST_CHECK_EQUAL(continuedDigest, expectedDigest);
}

BOOST_AUTO_TEST_SUITE_END()