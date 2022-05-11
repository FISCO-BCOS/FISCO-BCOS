#pragma once

#include "Exceptions.h"
#include "interfaces/crypto/Concepts.h"
#include <bcos-crypto/interfaces/crypto/hasher/Hasher.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <exception>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>

namespace bcos::tool
{

template <class Range, class HashType>
concept InputRange =
    std::ranges::random_access_range<Range> && bcos::crypto::TrivialObject<HashType> &&
    std::is_same_v<std::remove_cvref_t<std::remove_cvref_t<std::ranges::range_value_t<Range>>>,
        HashType>;

template <class Range, class HashType>
concept OutputRange = std::ranges::random_access_range<Range> &&
    std::ranges::output_range<Range, HashType> && bcos::crypto::TrivialObject<HashType>;

template <bcos::crypto::Hasher HasherType, class HashType, size_t stepSize = 2>
class BinaryMerkleTrie
{
public:
    static_assert(stepSize >= 2, "Step size too short!");

    BinaryMerkleTrie(){};

    void calculateAllHashes(InputRange<HashType> auto const& input)
    {
        auto const inputSize = std::size(input);
        if (inputSize <= 0)
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Input size too short!"});

        m_nodes.resize(inputSize + (inputSize - 1));

        std::copy_n(std::begin(input), inputSize, m_nodes.begin());
        std::sort(m_nodes.begin(), m_nodes.begin() + inputSize);

        auto range = std::ranges::subrange{m_nodes.begin(), m_nodes.begin() + inputSize};

        while (range.begin() != range.end())
        {
            auto length = calculateLevelHashes<HashType>(
                range, std::ranges::subrange(range.begin(), m_nodes.end()));
            range = std::ranges::subrange{range.end(), range.end() + length};
        }
    }

    size_t calculateLevelHashes(
        InputRange<HashType> auto&& input, OutputRange<HashType> auto&& output)
    {
        auto inputSize = std::size(input);
        auto outputSize = std::size(output);
        auto expectOutputSize = (inputSize + (stepSize - 1)) / stepSize;

        if (inputSize <= 0) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Input size too short!"});

        if (outputSize < expectOutputSize) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});

        ExceptionHolder holder;
#pragma omp parallel if (inputSize >= MIN_PARALLEL_SIZE)
        {
            HasherType hasher;
#pragma omp for
            for (decltype(inputSize) i = 0; i < inputSize; i += stepSize)
            {
                holder.run([&]() {
                    for (auto j = i; j < i + stepSize && j < inputSize; ++j)
                    {
                        hasher.update(input[j]);
                    }
                    hasher.final(output[i / stepSize]);
                });
            }
        }
        holder.rethrow();

        return expectOutputSize;
    }

private:
    std::vector<HashType> m_nodes;

    constexpr static size_t MIN_PARALLEL_SIZE = 32;
};
}  // namespace bcos::tool