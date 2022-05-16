#include <bcos-crypto/interfaces/crypto/hasher/OpenSSLHasher.h>
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
void testFixedWidthMerkle()
{
    for (auto count : std::ranges::iota_view{0, 64})
    {
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

BOOST_AUTO_TEST_CASE(merkle)
{
    testFixedWidthMerkle<2>();
    testFixedWidthMerkle<3>();
    testFixedWidthMerkle<4>();
    testFixedWidthMerkle<5>();
    testFixedWidthMerkle<6>();
    testFixedWidthMerkle<7>();
    testFixedWidthMerkle<16>();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test