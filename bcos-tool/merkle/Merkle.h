#pragma once

#include "bcos-concepts/Basic.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-utilities/Ranges.h>
#include <bits/ranges_base.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/endian.hpp>
#include <boost/format.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>

#include <variant>

namespace bcos::tool::merkle
{

template <class Range>
concept HashRange = RANGES::random_access_range<Range> &&
    bcos::concepts::bytebuffer::ByteBuffer<std::remove_cvref_t<RANGES::range_value_t<Range>>>;

template <class Range>
concept MerkleRange = HashRange<Range>;

template <class Range>
concept ProofRange = bcos::concepts::DynamicRange<Range> &&
    bcos::concepts::bytebuffer::ByteBuffer<std::remove_cvref_t<RANGES::range_value_t<Range>>>;

template <bcos::crypto::hasher::Hasher HasherType, size_t width = 2>
class Merkle
{
    static_assert(width >= 2, "Width too short, at least 2");
    static_assert(HasherType::HASH_SIZE >= 4, "Hash size too short!");

    using HashType = std::array<std::byte, HasherType::HASH_SIZE>;

public:
    bool verifyMerkleProof(ProofRange auto const& proof, bcos::concepts::bytebuffer::Hash auto hash,
        bcos::concepts::bytebuffer::Hash auto const& root)
    {
        if (RANGES::empty(proof)) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty input proof!"});

        if (RANGES::size(proof) > 1)
        {
            auto it = RANGES::begin(proof);

            while (it != RANGES::end(proof))
            {
                auto count = getNumberFromHash(*(it++));
                auto range = RANGES::subrange<decltype(it)>{it, it + count};

                if (RANGES::find(range, hash) == RANGES::end(range)) [[unlikely]]
                    return false;

                HasherType hasher;
                for (auto& merkleHash : range)
                {
                    hasher.update(merkleHash);
                }
                hasher.final(hash);

                std::advance(it, count);
            }
        }

        if (hash != root) [[unlikely]]
            return false;

        return true;
    }

    void generateMerkleProof(HashRange auto const& originHashes, MerkleRange auto const& merkle,
        bcos::concepts::bytebuffer::Hash auto const& hash, ProofRange auto& out) const
    {
        // Find the hash in originHashes first
        auto it = RANGES::find(originHashes, hash);
        if (it == RANGES::end(originHashes)) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Not found hash!"});

        generateMerkleProof(
            originHashes, merkle, RANGES::distance(RANGES::begin(originHashes), it), out);
    }

    void generateMerkleProof(HashRange auto const& originHashes, MerkleRange auto const& merkle,
        std::integral auto index, ProofRange auto& out) const
    {
        if ((size_t)index >= (size_t)RANGES::size(originHashes)) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Out of range!"});

        if (RANGES::size(originHashes) == 1)
        {
            concepts::resizeTo(out, 1);
            bcos::concepts::bytebuffer::assignTo(*RANGES::begin(merkle), *RANGES::begin(out));
            return;
        }

        using OutValueType = std::remove_cvref_t<RANGES::range_value_t<decltype(out)>>;

        auto [merkleNodes, merkleLevels] = getMerkleSize(RANGES::size(originHashes));
        if (merkleNodes != RANGES::size(merkle))
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Merkle size mismitch!"});

        index = indexAlign(index);
        auto count = std::min((size_t)(RANGES::size(originHashes) - index), (size_t)width);

        OutValueType number;
        setNumberToHash(count, number);
        out.emplace_back(std::move(number));

        for (auto it = RANGES::begin(originHashes) + index;
             it < RANGES::begin(originHashes) + index + count; ++it)
        {
            OutValueType hash;
            bcos::concepts::bytebuffer::assignTo(*it, hash);
            out.emplace_back(std::move(hash));
        }

        // Query next level hashes
        auto inputIt = RANGES::begin(merkle);
        while (inputIt != RANGES::end(merkle))
        {
            index = indexAlign(index / width);
            auto levelLength = getNumberFromHash(*(inputIt++));
            assert(index < levelLength);
            if (levelLength == 1)  // Ignore merkle root
                break;

            auto count = std::min((size_t)(levelLength - index), (size_t)width);

            OutValueType number;
            setNumberToHash(count, number);
            out.emplace_back(std::move(number));
            for (auto it = inputIt + index; it < inputIt + index + count; ++it)
            {
                OutValueType hash;
                bcos::concepts::bytebuffer::assignTo(*it, hash);
                out.emplace_back(hash);
            }
            RANGES::advance(inputIt, levelLength);
        }
    }

    void generateMerkle(HashRange auto const& originHashes, MerkleRange auto& out) const
    {
        if (RANGES::empty(originHashes)) [[unlikely]]
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty input"});

        if (RANGES::size(originHashes) == 1)
        {
            bcos::concepts::resizeTo(out, 1);
            bcos::concepts::bytebuffer::assignTo(*RANGES::begin(originHashes), *RANGES::begin(out));
            return;
        }

        [[maybe_unused]] auto [merkleNodes, merkleLevels] =
            getMerkleSize(RANGES::size(originHashes));
        bcos::concepts::resizeTo(out, merkleNodes);

        // Calculate first level from originHashes
        auto it = RANGES::begin(out);
        auto nextNodes = getNextLevelSize(RANGES::size(originHashes));
        setNumberToHash(nextNodes, *(it++));
        auto outputRange = RANGES::subrange<decltype(it)>(it, it + nextNodes);
        calculateLevelHashes(originHashes, outputRange);

        while (nextNodes > 1)  // Calculate next level from out, ignore only root
        {
            auto inputRange = RANGES::subrange<decltype(it)>(it, it + nextNodes);

            RANGES::advance(it, nextNodes);
            nextNodes = getNextLevelSize(nextNodes);

            setNumberToHash(nextNodes, *(it++));
            auto outputRange = RANGES::subrange<decltype(it)>(it, it + nextNodes);
            calculateLevelHashes(inputRange, outputRange);

            assert(it <= RANGES::end(out));
        }
    }

private:
    auto indexAlign(std::integral auto index) const { return index - ((index + width) % width); }

    void setNumberToHash(uint32_t number, bcos::concepts::bytebuffer::Hash auto& output) const
    {
        *((uint32_t*)output.data()) = boost::endian::native_to_big(number);
    }

    uint32_t getNumberFromHash(bcos::concepts::bytebuffer::Hash auto const& input) const
    {
        return boost::endian::big_to_native(*((uint32_t*)input.data()));
    }

    std::tuple<unsigned, unsigned> getMerkleSize(std::integral auto inputSize) const
    {
        auto nodeSize = 0u;
        auto levels = 0u;
        while (inputSize > 1)
        {
            inputSize = getNextLevelSize(inputSize);
            nodeSize += (inputSize + 1);  // Extra 1 for length record
            ++levels;
        }

        return std::make_tuple(nodeSize, levels);
    }

    auto getNextLevelSize(std::integral auto inputSize) const
    {
        return (inputSize + (width - 1)) / width;
    }

    void calculateLevelHashes(HashRange auto const& input, HashRange auto& output) const
    {
        assert(RANGES::size(input) > 0);

        auto outputSize = RANGES::size(output);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, outputSize),
            [this, &input, &output](const tbb::blocked_range<size_t>& range) {
                HasherType hasher;

                for (auto i = range.begin(); i < range.end(); ++i)
                {
                    for (auto j = i * width; j < (i + 1) * width && j < RANGES::size(input); ++j)
                    {
                        hasher.update(input[j]);
                    }
                    hasher.final(output[i]);
                }
            });
    }
};

}  // namespace bcos::tool::merkle