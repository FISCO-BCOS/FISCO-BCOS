#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-tool/BinaryMerkleTrie.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <iterator>

namespace bcos::test
{
struct TestBinaryMerkleTrieFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestBinaryMerkleTrie, TestBinaryMerkleTrieFixture)

BOOST_AUTO_TEST_CASE(testCalcHash)
{
    bcos::tool::BinaryMerkleTrie<bcos::crypto::openssl::OpenSSL_SHA3_256_Hasher> trie;

    size_t count = 100000;
    std::vector<bcos::h256> hashes;
    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        hashes.emplace_back(i);
    }

    std::vector<bcos::h256> result(count / 2);
    trie.parseRange(hashes, result);
}

BOOST_AUTO_TEST_CASE(testCalc)
{
    bcos::tool::BinaryMerkleTrie<bcos::crypto::openssl::OpenSSL_SHA3_256_Hasher> trie;

    size_t count = 100000;
    std::vector<bcos::h256> hashes;

    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        hashes.emplace_back(i);
    }

    trie.calcHash(hashes);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test