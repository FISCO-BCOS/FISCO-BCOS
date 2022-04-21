#pragma once

#include <bcos-crypto/interfaces/crypto/Hashing.h>
#include <boost/concept/assert.hpp>
#include <boost/concept_check.hpp>
#include <boost/range.hpp>
#include <boost/range/concepts.hpp>
#include <algorithm>
#include <type_traits>

namespace bcos::tool
{
class BinaryMerkleTrie
{
public:
    template <class Range, class Out, class HashType>
    void parseRange(Range&& range, Out out)
    {
        using RangeType = std::remove_reference<std::remove_cv<Range>>;

        BOOST_CONCEPT_ASSERT((boost::RandomAccessRangeConcept<RangeType>));
        static_assert(std::is_same_v<boost::range_value<RangeType>, bcos::h256>,
            "Input range value type isn't bcos::h256!");
        static_assert(
            std::is_base_of_v<crypto::Hashing<HashType>, HashType>, "Input HashType mismatch!");
        BOOST_CONCEPT_ASSERT((boost::Mutable_ForwardIteratorConcept<Out>));

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
                *out++ = (localHasher << *(begin + i) << *(begin + i + 1)).final();
            }
            else
            {
                *out++ = (localHasher << *(begin + i)).final();
            }

#pragma omp atomic
            ++count;
        }

        if (count > 1)
        {
            parseRange(std::make_tuple(outBegin, out), out);
        }
    }

private:
    std::vector<bcos::h256> m_nodes;
};
}  // namespace bcos::tool