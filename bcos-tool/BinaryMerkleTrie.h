#pragma once

#include "Exceptions.h"
#include <bcos-crypto/interfaces/crypto/Hasher.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <exception>
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

template <bcos::crypto::Hasher Hasher>
class BinaryMerkleTrie
{
public:
    BinaryMerkleTrie(){};

    void calcHash(InputHashRange auto const& input)
    {
        auto inputSize = std::size(input);
        if (inputSize <= 0)
        {
            BOOST_THROW_EXCEPTION(MerkleTrieException{});
        }

        m_nodes.resize(inputSize + (inputSize - 1));

        std::copy_n(std::begin(input), std::size(input), std::begin(m_nodes));

        auto range = std::make_pair(m_nodes.begin(), m_nodes.begin() + inputSize);
        while (range.first != range.second)
        {
            auto length = parseRange(range, std::make_pair(range.second, m_nodes.end()));
            range.first = range.second;
            range.second += length;
        }
    }

    size_t parseRange(InputHashRange auto const& input, OutputHashRange auto& output)
    {
        auto inputSize = std::size(input);
        auto outputSize = std::size(output);

        auto expectOutputSize = (inputSize + 1) / 2;

        if (outputSize < expectOutputSize)
        {
            BOOST_THROW_EXCEPTION(MerkleTrieException{});
        }

        ExceptionHolder holder;
#pragma omp parallel
        {
            Hasher localHasher;
#pragma omp for
            for (size_t i = 0; i < inputSize; i += 2)
            {
                holder.run([&]() {
                    if (i + 1 < inputSize) [[likely]]
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
                });
            }
        }
        holder.rethrow();

        return expectOutputSize;
    }

    std::vector<bcos::h256> getProof(bcos::h256 hash)
    {
        return {};
    }

private:
    std::vector<bcos::h256> m_nodes;
};
}  // namespace bcos::tool