#include <bcos-crypto/interfaces/crypto/hasher/OpenSSLHasher.h>
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
    bcos::tool::MerkleTrie<bcos::crypto::openssl::OpenSSL_SHA3_256_Hasher,
        std::array<std::byte, 32>>
        trie;

    size_t count = 100000;
    std::vector<std::array<std::byte, 32>> hashes;
    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        crypto::openssl::OpenSSL_SHA3_256_Hasher hasher;
        hasher.update(i);
        hashes.push_back(hasher.final());
    }

    std::vector<std::array<std::byte, 32>> result(count / 2);
    trie.calculateLevelHashes(hashes, result);
}

BOOST_AUTO_TEST_CASE(testCalc)
{
    bcos::tool::MerkleTrie<bcos::crypto::openssl::OpenSSL_SHA3_256_Hasher,
        std::array<std::byte, 32>>
        trie;

    size_t count = 100000;
    std::vector<std::array<std::byte, 32>> hashes;

    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        crypto::openssl::OpenSSL_SHA3_256_Hasher hasher;
        hasher.update(i);
        hashes.push_back(hasher.final());
    }

    trie.calculateAllHashes(hashes);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test