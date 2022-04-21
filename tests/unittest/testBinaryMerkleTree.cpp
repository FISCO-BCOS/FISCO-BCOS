#include <bcos-crypto/hashing/OpenSSLHashing.h>
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

    size_t count = 1000;
    std::vector<bcos::h256> hashes;
    hashes.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        hashes.emplace_back(i);
    }

    std::vector<bcos::h256> result;
    trie.parseRange<bcos::crypto::openssl::SHA3_256Hashing>(hashes, std::back_inserter(result));
    // trie.parseRange<std::string>(hashes, std::back_inserter(result));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test