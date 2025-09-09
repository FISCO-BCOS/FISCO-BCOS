#pragma once

#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/endian.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace bcos::crypto::merkle
{

template <class Range>
concept HashRange =
    ::ranges::random_access_range<Range> &&
    bcos::concepts::bytebuffer::ByteBuffer<std::remove_cvref_t<::ranges::range_value_t<Range>>>;

template <class Range>
concept MerkleRange = HashRange<Range>;

template <class Range>
concept ProofRange =
    bcos::concepts::DynamicRange<Range> &&
    bcos::concepts::bytebuffer::ByteBuffer<std::remove_cvref_t<::ranges::range_value_t<Range>>>;

template <bcos::crypto::hasher::Hasher HasherType, size_t width = 2>
class Merkle
{
    static_assert(width >= 2, "Width too short, at least 2");
    using HashType = bcos::bytes;

public:
    Merkle(HasherType hasher) : m_hasher(std::move(hasher)) { assert(m_hasher.hashSize() > 4); }

    bool verifyMerkleProof(ProofRange auto const& proof, bcos::concepts::bytebuffer::Hash auto hash,
        bcos::concepts::bytebuffer::Hash auto const& root)
    {
        if (::ranges::empty(proof)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty input proof!"});
        }

        if (::ranges::size(proof) > 1)
        {
            for (auto it = ::ranges::begin(proof); it != ::ranges::end(proof);)
            {
                auto count = getNumberFromHash(*(it++));
                auto range = ::ranges::subrange<decltype(it)>{it, it + count};

                if (::ranges::find(range, hash) == ::ranges::end(range)) [[unlikely]]
                {
                    return false;
                }

                auto hasher = m_hasher.clone();
                for (auto& merkleHash : range)
                {
                    hasher.update(merkleHash);
                }
                hasher.final(hash);

                std::advance(it, count);
            }
        }

        if (hash != root) [[unlikely]]
        {
            return false;
        }

        return true;
    }

    void generateMerkleProof(HashRange auto originHashes,
        bcos::concepts::bytebuffer::Hash auto const& hash, ProofRange auto& out) const
    {
        // Find the hash in originHashes first
        auto it = ::ranges::find(originHashes, hash);
        if (it == ::ranges::end(originHashes)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Not found hash!"});
        }

        std::vector<HashType> merkle;
        generateMerkle(::ranges::views::all(originHashes), merkle);
        generateMerkleProof(::ranges::views::all(originHashes), merkle,
            ::ranges::distance(::ranges::begin(originHashes), it), out);
    }

    void generateMerkleProof(HashRange auto originHashes, MerkleRange auto const& merkle,
        bcos::concepts::bytebuffer::Hash auto const& hash, ProofRange auto& out) const
    {
        // Find the hash in originHashes first
        auto it = ::ranges::find(originHashes, hash);
        if (it == ::ranges::end(originHashes)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Not found hash!"});
        }
        generateMerkleProof(::ranges::views::all(originHashes), merkle,
            ::ranges::distance(::ranges::begin(originHashes), it), out);
    }

    void generateMerkleProof(HashRange auto originHashes, MerkleRange auto const& merkle,
        std::integral auto index, ProofRange auto& out) const
    {
        if ((size_t)index >= (size_t)::ranges::size(originHashes)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Out of range!"});
        }

        if (::ranges::size(originHashes) == 1)
        {
            concepts::resizeTo(out, 1);
            bcos::concepts::bytebuffer::assignTo(*::ranges::begin(merkle), *::ranges::begin(out));
            return;
        }

        auto [merkleNodes, merkleLevels] = getMerkleSize(::ranges::size(originHashes));
        if (merkleNodes != ::ranges::size(merkle))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Merkle size mismatch!"});
        }

        index = indexAlign(index);
        auto count = std::min((size_t)(::ranges::size(originHashes) - index), (size_t)width);

        setNumberToHash(count, out.emplace_back());
        for (auto it = ::ranges::begin(originHashes) + index;
            it < ::ranges::begin(originHashes) + index + count; ++it)
        {
            bcos::concepts::bytebuffer::assignTo(*it, out.emplace_back());
        }

        // Query next level hashes
        auto inputIt = ::ranges::begin(merkle);
        while (inputIt != ::ranges::end(merkle))
        {
            index = indexAlign(index / width);
            auto levelLength = getNumberFromHash(*(inputIt++));
            assert(index < levelLength);
            if (levelLength == 1)
            {  // Ignore merkle root
                break;
            }

            auto nextCount = std::min((size_t)(levelLength - index), (size_t)width);

            setNumberToHash(nextCount, out.emplace_back());
            for (auto it = inputIt + index; it < inputIt + index + nextCount; ++it)
            {
                bcos::concepts::bytebuffer::assignTo(*it, out.emplace_back());
            }
            ::ranges::advance(inputIt, levelLength);
        }
    }

    void generateMerkle(HashRange auto originHashes, MerkleRange auto& out) const
    {
        if (::ranges::empty(originHashes))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Empty input"});
        }

        if (::ranges::size(originHashes) == 1)
        {
            bcos::concepts::resizeTo(out, 1);
            bcos::concepts::bytebuffer::assignTo(
                *::ranges::begin(originHashes), *::ranges::begin(out));
            return;
        }

        [[maybe_unused]] auto [merkleNodes, merkleLevels] =
            getMerkleSize((unsigned)::ranges::size(originHashes));
        bcos::concepts::resizeTo(out, merkleNodes);

        // Calculate first level from originHashes
        auto it = ::ranges::begin(out);
        auto nextNodes = getNextLevelSize((unsigned)::ranges::size(originHashes));
        setNumberToHash(nextNodes, *(it++));
        auto outputRange = ::ranges::subrange<decltype(it)>(it, it + nextNodes);
        calculateLevelHashes(::ranges::views::all(originHashes), outputRange);

        while (nextNodes > 1)  // Calculate next level from out, ignore only root
        {
            auto inputRange = ::ranges::subrange<decltype(it)>(it, it + nextNodes);

            ::ranges::advance(it, nextNodes);
            nextNodes = getNextLevelSize(nextNodes);

            setNumberToHash(nextNodes, *(it++));
            auto nextOutputRange = ::ranges::subrange<decltype(it)>(it, it + nextNodes);
            calculateLevelHashes(inputRange, nextOutputRange);

            assert(it <= ::ranges::end(out));
        }
    }

private:
    HasherType m_hasher;

    auto indexAlign(std::integral auto index) const { return index - ((index + width) % width); }

    void setNumberToHash(uint32_t number, bcos::concepts::bytebuffer::Hash auto& output) const
    {
        bcos::concepts::resizeTo(output, sizeof(uint32_t));
        *((uint32_t*)output.data()) = boost::endian::native_to_big(number);
    }

    uint32_t getNumberFromHash(bcos::concepts::bytebuffer::Hash auto const& input) const
    {
        return boost::endian::big_to_native(*((uint32_t*)input.data()));
    }

    std::tuple<unsigned, unsigned> getMerkleSize(std::integral auto inputSize) const
    {
        auto nodeSize = 0U;
        auto levels = 0U;
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

    void calculateLevelHashes(HashRange auto input, HashRange auto& output) const
    {
        assert(::ranges::size(input) > 0);

        auto outputSize = ::ranges::size(output);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, outputSize),
            [this, &input, &output](const tbb::blocked_range<size_t>& range) {
                auto hasher = m_hasher.clone();

                for (auto i = range.begin(); i < range.end(); ++i)
                {
                    for (auto j = i * width; j < (i + 1) * width && j < ::ranges::size(input); ++j)
                    {
                        hasher.update(input[j]);
                    }
                    hasher.final(output[i]);
                }
            });
    }
};

}  // namespace bcos::crypto::merkle