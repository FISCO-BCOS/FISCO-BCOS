#pragma once

#include <bcos-crypto/interfaces/crypto/Hasher.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <type_traits>

namespace bcos::tool
{
struct MerkleTrieException : public virtual boost::exception, public virtual std::exception
{
};

template <class Range>
concept InputHashRange = std::ranges::random_access_range<Range> &&
    std::is_same_v<std::ranges::range_value_t<Range>, bcos::h256>;

template <class Range>
concept OutputHashRange =
    std::ranges::random_access_range<Range> && std::ranges::output_range<Range, bcos::h256>;

class BinaryMerkleTrie
{
public:
    BinaryMerkleTrie(){};

    // Accept random access range
    // explicit BinaryMerkleTrie(auto&& input)
    // {
    //     auto nodeSize = std::size(input);
    //     if (nodeSize <= 0)
    //     {
    //         BOOST_THROW_EXCEPTION(MerkleTrieException{});
    //     }

    //     nodeSize += (nodeSize - 1);
    //     m_nodes.resize(nodeSize);
    // }

    template <bcos::crypto::Hasher Hasher>
    void parseRange(InputHashRange auto const& input, OutputHashRange auto& output)
    {
        auto inputSize = std::size(input);
        auto outputSize = std::size(output);

        if (outputSize < (inputSize + 1) / 2)
        {
            BOOST_THROW_EXCEPTION(MerkleTrieException{});
        }

#pragma omp parallel
        {
            Hasher localHasher;
#pragma omp for
            for (size_t i = 0; i < inputSize; i += 2)
            {
                if (i + 1 < inputSize)
                {
                    auto& hash1 = input[i];
                    auto& hash2 = input[i + 1];
                    localHasher.update(std::span<byte const>{hash1.data(), hash1.size});
                    localHasher.update(std::span<byte const>{hash2.data(), hash2.size});
                    output[i / 2] = localHasher.final();
                }
                else
                {
                    auto& hash = input[i];
                    localHasher.update(std::span<byte const>{hash.data(), hash.size});
                    output[i / 2] = localHasher.final();
                }
            }
        }
    }

private:
    std::vector<bcos::h256> m_nodes;
};
}  // namespace bcos::tool