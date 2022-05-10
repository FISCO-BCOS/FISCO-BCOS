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
    std::is_same_v<std::remove_cvref_t<std::ranges::range_value_t<Range>>, HashType>;

template <class Range, class HashType>
concept OutputRange = std::ranges::random_access_range<Range> &&
    std::ranges::output_range<Range, HashType> && bcos::crypto::TrivialObject<HashType>;

template <bcos::crypto::Hasher HasherType>
class BinaryMerkleTrie
{
public:
    BinaryMerkleTrie(){};

    template <class HashType>
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
            auto length =
                calculateLevelHashes(range, std::ranges::subrange(range.begin(), m_nodes.end()));
            range = {range.end(), range.end() + length};
        }
    }

    template <class HashType>
    size_t calculateLevelHashes(
        InputRange<HashType> auto&& input, OutputRange<HashType> auto& output)
    {
        auto inputSize = std::size(input);
        auto outputSize = std::size(output);
        auto expectOutputSize = (inputSize + 1) / 2;

        if (inputSize <= 0)
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Input size too short!"});

        if (outputSize < expectOutputSize)
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});

        using FuncType = decltype([](){ return 0; });
        FuncType f;

        ExceptionHolder holder;
#pragma omp parallel if (inputSize >= MIN_PARALLEL_SIZE)
        {
            HasherType localHasher;
#pragma omp for
            for (size_t i = 0; i < inputSize; i += 2)
            {
                holder.run([&]() {
                    if (i + 1 < inputSize) [[likely]]
                    {
                        localHasher.update(input[i]);
                        localHasher.update(input[i + 1]);
                        localHasher.final(output[i / 2]);
                    }
                    else
                    {
                        localHasher.update(input[i]);
                        localHasher.final(output[i / 2]);
                    }
                });
            }
        }
        holder.rethrow();

        return expectOutputSize;
    }

private:
    std::vector<std::array<std::byte, 32>> m_nodes;

    constexpr static size_t MIN_PARALLEL_SIZE = 32;
};
}  // namespace bcos::tool