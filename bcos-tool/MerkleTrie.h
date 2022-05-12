#pragma once

#include "Exceptions.h"
#include "interfaces/crypto/Concepts.h"
#include <bcos-crypto/interfaces/crypto/hasher/Hasher.h>
#include <boost/format.hpp>
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

template <class Range, class HashType>
concept Proof =
    std::ranges::input_range<Range> && std::is_same_v<std::ranges::range_value_t<Range>, HashType>;

template <bcos::crypto::Hasher HasherType, class HashType, size_t width = 2>
class Merkle
{
public:
    static_assert(width >= 2, "Width too short, at least 2");

    Merkle() = default;
    Merkle(const Merkle&) = default;
    Merkle(Merkle&&) = default;
    Merkle& operator=(const Merkle&) = default;
    Merkle& operator=(Merkle&&) = default;
    ~Merkle() = default;

    static bool verify(Proof<HashType> auto&& proof, HashType&& root)
    {
        if (std::empty(proof))
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty proof input!"});

        size_t count = 0;
        std::optional<HashType> lastHash;
        HasherType hasher;
        for (auto& hash : proof)
        {
            if (lastHash)
            {
                hasher.update(*lastHash);
                hasher.update(hash);
                hasher.final(*lastHash);
            }
            else
            {
                hasher.update(hash);
                lastHash.emplace(hash);
            }

            ++count;
        }
    }

    HashType import(InputRange<HashType> auto&& input, bool parallel = false)
    {
        auto inputSize = std::size(input);
        if (inputSize <= 0)
            BOOST_THROW_EXCEPTION(std::invalid_argument{
                (boost::format{"Input size too short: %ld"} % inputSize).str()});

        m_count = inputSize;
        m_nodes.resize(getNodeSize(inputSize));

        std::copy_n(std::begin(input), inputSize, m_nodes.begin());
        std::sort(m_nodes.begin(), m_nodes.begin() + inputSize);

        auto range = std::ranges::subrange{m_nodes.begin(), m_nodes.begin() + inputSize};

        while (range.begin() != range.end())
        {
            assert(range.end() <= m_nodes.end());
            auto length = calculateLevelHashes(
                range, std::ranges::subrange(range.begin(), m_nodes.end()), parallel);

            range = std::ranges::subrange{range.end(), range.end() + length};
        }

        return *m_nodes.rbegin();
    }

    void clear()
    {
        m_nodes.clear();
        m_count = 0;
    }
    auto empty() const { return !m_count; }

private:
    auto getNodeSize(std::integral auto inputSize)
    {
        auto nodeSize = inputSize;
        while (inputSize > 1)
        {
            inputSize = getLevelSize(inputSize);
            nodeSize += inputSize;
        }

        return nodeSize;
    }

    auto getLevelSize(std::integral auto inputSize)
    {
        return inputSize == 1 ? 0 : (inputSize + (width - 1)) / width;
    }

    size_t calculateLevelHashes(
        InputRange<HashType> auto&& input, OutputRange<HashType> auto&& output, bool parallel)
    {
        auto inputSize = std::size(input);
        auto outputSize = std::size(output);
        auto expectOutputSize = getLevelSize(inputSize);

        assert(inputSize > 0);
        assert(outputSize >= expectOutputSize);

        ExceptionHolder holder;
#pragma omp parallel if ((inputSize >= MIN_PARALLEL_SIZE) && parallel)
        {
            HasherType hasher;
#pragma omp for
            for (decltype(inputSize) i = 0; i < inputSize; i += width)
            {
                holder.run([&]() {
                    for (auto j = i; j < i + width && j < inputSize; ++j)
                    {
                        hasher.update(input[j]);
                    }
                    auto outputOffset = i / width;
                    assert(outputOffset < outputSize);

                    hasher.final(output[outputOffset]);
                });
            }
        }
        holder.rethrow();

        return expectOutputSize;
    }

private:
    std::vector<HashType> m_nodes;
    size_t m_count = 0;

    constexpr static size_t MIN_PARALLEL_SIZE = 32;
};

}  // namespace bcos::tool