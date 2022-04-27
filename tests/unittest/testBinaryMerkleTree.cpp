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
    bcos::tool::BinaryMerkleTrie trie;

    size_t count = 100000;
    std::vector<bcos::h256> hashes;
    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        hashes.emplace_back(i);
    }

    std::vector<bcos::h256> result(100000);
    std::vector<std::string> resultFake;
    trie.parseRange<bcos::crypto::openssl::OpenSSL_SHA2_256_Hasher>(hashes, result);
    // trie.parseRange<std::string>(hashes, std::back_inserter(result));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test