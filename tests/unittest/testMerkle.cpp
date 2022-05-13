#include <bcos-crypto/interfaces/crypto/hasher/OpenSSLHasher.h>
#include <bcos-tool/Merkle.h>
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

BOOST_AUTO_TEST_CASE(import)
{
    using HashType = std::array<std::byte, 32>;

    auto count = 100 * 10000;
    std::vector<HashType> hashes(count);

#pragma omp parallel
    {
        crypto::openssl::OpenSSL_SHA3_256_Hasher hasher;
#pragma omp for
        for (decltype(count) i = 0; i < count; ++i)
        {
            hasher.update(i);
            hashes[i] = hasher.final();
        }
    }

    bcos::tool::Merkle<bcos::crypto::openssl::OpenSSL_SHA3_256_Hasher, HashType> trie;
    trie.import(hashes);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test