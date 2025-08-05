#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/hex.hpp>
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <ostream>
#include <random>
#include <stdexcept>

using HashType = std::array<uint8_t, 32>;

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

using namespace bcos::crypto::merkle;
using MerkleProof = std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>;
using MerkleProofPtr = std::shared_ptr<MerkleProof>;

struct TestBinaryMerkleTrieFixture
{
    std::array<HashType, 64> hashes;

    TestBinaryMerkleTrieFixture()
    {
        crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher hasher;
        std::mt19937 prng(std::random_device{}());

        for (auto& element : hashes)
        {
            hasher.update(prng());
            hasher.final(element);
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(TestBinaryMerkleTrie, TestBinaryMerkleTrieFixture)

template <size_t width>
void testFixedWidthMerkle(bcos::crypto::merkle::HashRange auto const& inputHashes)
{
    HashType emptyHash;
    emptyHash.fill(0);
    auto seed = std::random_device{}();

    for (auto count = 0lu; count < ::ranges::size(inputHashes); ++count)
    {
        std::span<HashType const> hashes(inputHashes.data(), count);

        bcos::crypto::merkle::Merkle<bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher, width>
            trie(bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher{});
        std::vector<HashType> out;
        BOOST_CHECK_THROW(trie.generateMerkle(std::vector<HashType>{}, out),
            boost::wrapexcept<std::invalid_argument>);

        if (count == 0)
        {
            BOOST_CHECK_THROW(trie.generateMerkle(std::as_const(hashes), out),
                boost::wrapexcept<std::invalid_argument>);
        }
        else
        {
            std::vector<HashType> outMerkle;
            BOOST_CHECK_NO_THROW(trie.generateMerkle(std::as_const(hashes), outMerkle));
            std::cout << "Merkle: " << std::endl;
            std::cout << outMerkle << std::endl;

            std::vector<HashType> outProof;
            BOOST_CHECK_THROW(trie.generateMerkleProof(hashes, outMerkle, emptyHash, outProof),
                boost::wrapexcept<std::invalid_argument>);
            BOOST_CHECK_THROW(
                trie.generateMerkleProof(hashes, outMerkle, ::ranges::size(hashes), outProof),
                boost::wrapexcept<std::invalid_argument>);

            for (auto& hash : hashes)
            {
                trie.generateMerkleProof(hashes, outMerkle, hash, outProof);

                std::cout << "Width: " << width << " Root: " << *outMerkle.rbegin()
                          << " Hash: " << hash << std::endl;

                std::cout << "Proof: " << std::endl;
                std::cout << outProof << std::endl;
                BOOST_TEST(trie.verifyMerkleProof(outProof, hash, *(outMerkle.rbegin())));
                BOOST_TEST(!trie.verifyMerkleProof(outProof, emptyHash, *(outMerkle.rbegin())));

                auto dis = std::uniform_int_distribution<size_t>(0lu, outProof.size() - 1);
                std::mt19937 prng{seed};
                outProof[dis(prng)] = emptyHash;

                if (outProof.size() > 1)
                {
                    BOOST_TEST(!trie.verifyMerkleProof(outProof, hash, *(outMerkle.rbegin())));
                }

                outProof.clear();
                BOOST_CHECK_THROW(trie.verifyMerkleProof(outProof, hash, *(outMerkle.rbegin())),
                    boost::wrapexcept<std::invalid_argument>);
            }
        }
    }
}

template <size_t i>
constexpr void loopWidthTest(bcos::crypto::merkle::HashRange auto const& inputHashes)
{
    testFixedWidthMerkle<i>(inputHashes);

    if constexpr (i > 2)
    {
        loopWidthTest<i - 1>(inputHashes);
    }
}

BOOST_AUTO_TEST_CASE(merkle)
{
    constexpr static size_t testCount = 16;
    loopWidthTest<testCount>(hashes);
}

template <typename Hasher>
std::shared_ptr<std::string> calculateRootByMerkleProof(
    const bcos::bytes& _txHash, MerkleProofPtr merkleProof, Hasher& hasher)
{
    auto txHash = _txHash;
    for (auto& oneLevel : *merkleProof)
    {
        auto& left = oneLevel.first;
        auto& right = oneLevel.second;
        // left
        std::for_each(left.begin(), left.end(),
            [&hasher](const std::string& _hash) { hasher.update(*fromHexString(_hash)); });
        hasher.update(txHash);
        // right
        std::for_each(right.begin(), right.end(),
            [&hasher](const std::string& _hash) { hasher.update(*fromHexString(_hash)); });
        hasher.final(txHash);
    }
    return toHexString(txHash);
}

BOOST_AUTO_TEST_CASE(performance) {}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test