#include "bcos-crypto/hasher/AnyHasher.h"
#include "bcos-crypto/hasher/OpenSSLHasher.h"
#include <boost/test/unit_test.hpp>
#include <array>
#include <cstdint>

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
    std::string input = "str123456789012345678901234567890";
    hasher.update(input);
    auto clone = hasher.clone();

    std::string parentDigest;
    hasher.digest(parentDigest);
    std::string cloneDigest;
    clone.digest(cloneDigest);
    BOOST_CHECK_NE(parentDigest, cloneDigest);

    AnyHasher empty(openssl::OpenSSL_SHA3_256_Hasher{});
    std::string emptyDigest;
    empty.digest(emptyDigest);
    BOOST_CHECK_EQUAL(cloneDigest, emptyDigest);
    BOOST_CHECK_EQUAL(clone.hashSize(), cloneDigest.size());
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

BOOST_AUTO_TEST_CASE(testOutputValidation)
{
    AnyHasher hasher(openssl::OpenSSL_Keccak256_Hasher{});
    std::array<std::byte, 31> shortOutput{};
    BOOST_CHECK_THROW(
        hasher.digest(std::span<std::byte>{shortOutput}), boost::wrapexcept<std::invalid_argument>);
    BOOST_CHECK_THROW(
        hasher.final(std::span<std::byte>{shortOutput}), boost::wrapexcept<std::invalid_argument>);
    BOOST_CHECK_THROW(hasher.digest(shortOutput), boost::wrapexcept<std::runtime_error>);
    BOOST_CHECK_THROW(hasher.final(shortOutput), boost::wrapexcept<std::runtime_error>);

    std::array<std::uint32_t, 8> wideOutput{};
    BOOST_CHECK_THROW(hasher.digest(wideOutput), boost::wrapexcept<std::runtime_error>);
    BOOST_CHECK_THROW(hasher.final(wideOutput), boost::wrapexcept<std::runtime_error>);
}

BOOST_AUTO_TEST_SUITE_END()
