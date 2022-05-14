#include <bcos-crypto/interfaces/crypto/hasher/OpenSSLHasher.h>
#include <bcos-tool/Merkle.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <future>
#include <iterator>
#include <stdexcept>

namespace bcos::test
{

using HashType = std::array<std::byte, 32>;
struct TestBinaryMerkleTrieFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestBinaryMerkleTrie, TestBinaryMerkleTrieFixture)

BOOST_AUTO_TEST_CASE(merkle)
{
    auto count = 32;
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
    BOOST_CHECK_THROW(
        trie.import(std::vector<HashType>{}), boost::wrapexcept<std::invalid_argument>);

    BOOST_CHECK_NO_THROW(trie.import(std::as_const(hashes)));

    HashType emptyHash;
    BOOST_CHECK_THROW(trie.generateProof(emptyHash), boost::wrapexcept<std::invalid_argument>);

    for (auto& hash : hashes)
    {
        auto proof = trie.generateProof(hash);
        BOOST_CHECK(trie.verifyProof(proof, hash, trie.root()));

        BOOST_CHECK(!trie.verifyProof(proof, emptyHash, trie.root()));

        proof.hashes.clear();
        BOOST_CHECK_THROW(trie.verifyProof(proof, emptyHash, trie.root()),
            boost::wrapexcept<std::invalid_argument>);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test