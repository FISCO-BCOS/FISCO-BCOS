#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-tool/Merkle.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <future>
#include <iterator>
#include <ostream>
#include <stdexcept>

using HashType = std::array<std::byte, 32>;

namespace std
{
std::ostream& operator<<(std::ostream& stream, const HashType& hash)
{
    std::string hex;
    boost::algorithm::hex_lower(
        (char*)hash.data(), (char*)hash.data() + hash.size(), std::back_inserter(hex));
    std::string_view view{hex.data(), 8};
    stream << view;
    return stream;
}
}  // namespace std

namespace bcos::test
{
struct TestBinaryMerkleTrieFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestBinaryMerkleTrie, TestBinaryMerkleTrieFixture)

template <size_t width>
void testFixedWidthMerkle(bcos::tool::InputRange<HashType> auto const& inputHashes)
{
    for (auto count = 0lu; count < std::size(inputHashes); ++count)
    {
        std::span<HashType const> hashes(inputHashes.data(), count);

        bcos::tool::Merkle<bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher, HashType, width>
            trie;
        BOOST_CHECK_THROW(
            trie.import(std::vector<HashType>{}), boost::wrapexcept<std::invalid_argument>);

        if (count == 0)
        {
            BOOST_CHECK_THROW(
                trie.import(std::as_const(hashes)), boost::wrapexcept<std::invalid_argument>);
        }
        else
        {
            BOOST_CHECK_NO_THROW(trie.import(std::as_const(hashes)));
            // std::cout << trie;

            HashType emptyHash;
            BOOST_CHECK_THROW(
                trie.generateProof(emptyHash), boost::wrapexcept<std::invalid_argument>);

            for (auto& hash : hashes)
            {
                auto proof = trie.generateProof(hash);
                // std::cout << proof;

                BOOST_CHECK(trie.verifyProof(proof, hash, trie.root()));

                BOOST_CHECK(!trie.verifyProof(proof, emptyHash, trie.root()));

                proof.hashes.clear();
                BOOST_CHECK_THROW(trie.verifyProof(proof, emptyHash, trie.root()),
                    boost::wrapexcept<std::invalid_argument>);
            }
        }
    }
}

template <size_t i>
constexpr void loopWidthTest(bcos::tool::InputRange<HashType> auto const& inputHashes)
{
    testFixedWidthMerkle<i>(inputHashes);

    if constexpr (i > 2)
    {
        loopWidthTest<i - 1>(inputHashes);
    }
}

BOOST_AUTO_TEST_CASE(merkle)
{
    std::array<HashType, 128> hashes;
#pragma omp parallel
    {
        crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher hasher;
#pragma omp for
        for (auto i = 0lu; i < hashes.size(); ++i)
        {
            hasher.update(i);
            hashes[i] = hasher.final();
        }
    }

    loopWidthTest<32>(hashes);
}

BOOST_AUTO_TEST_CASE(performance) {}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test