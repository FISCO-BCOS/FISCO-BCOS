#pragma once

#include <bcos-crypto/interfaces/crypto/Hashing.h>
#include <boost/concept/assert.hpp>
#include <boost/concept_check.hpp>
#include <boost/iterator/iterator_traits.hpp>
#include <boost/range.hpp>
#include <boost/range/concepts.hpp>
#include <algorithm>
#include <type_traits>

namespace bcos::tool
{
class BinaryMerkleTrie
{
public:
    template <class HashType, class Range, class Out>
    void parseRange(Range& range, Out out)
    {
        BOOST_CONCEPT_ASSERT((boost::RandomAccessRangeConcept<Range>));
        static_assert(std::is_same_v<boost::range_value<Range>, bcos::h256>,
            "Input range value type isn't bcos::h256!");
        static_assert(
            std::is_base_of_v<crypto::Hashing<HashType>, HashType>, "Input HashType mismatch!");
        BOOST_CONCEPT_ASSERT((boost::Mutable_ForwardIteratorConcept<Out>));
        static_assert(std::is_same_v<boost::iterator_value<Out>, bcos::h256>,
            "Output iterator value type isn't bcos::h256!");

        size_t count = 0;
        auto begin = boost::begin(range);
        auto total = boost::size(range);
        auto outBegin = out;

#pragma omp parallel
        HashType localHasher;
#pragma omp for
        for (size_t i = 0; i < total; i += 2)
        {
            if (i + 1 < total)
            {
                auto& hash1 = *(begin + i);
                auto& hash2 = *(begin + i + 1);
                *out++ = localHasher.update(gsl::span<byte const>(hash1.data(), hash1.size))
                             .update(gsl::span<byte const>(hash2.data(), hash2.size))
                             .final();
            }
            else
            {
                auto& hash = *(begin + i);
                *out++ = localHasher.update(gsl::span<byte const>(hash.data(), hash.size)).final();
            }

#pragma omp atomic
            ++count;
        }
    }

private:
    std::vector<bcos::h256> m_nodes;
};
}  // namespace bcos::tool