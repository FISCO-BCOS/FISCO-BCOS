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
class BinaryMerkleTree
{
public:
    template <class Range, class Out, class HashImpl>
    void parseRange(Range&& range, Out out, bcos::crypto::Hashing<HashImpl>&& hasher)
    {
        using RangeType = std::remove_reference<std::remove_cv<Range>>;
        static_assert(std::is_same_v<boost::range_value<RangeType>, bcos::h256>,
            "Input range value type isn't bcos::h256!");
        BOOST_CONCEPT_ASSERT((boost::Mutable_ForwardIteratorConcept<Out>));
        BOOST_CONCEPT_ASSERT((boost::RandomAccessRangeConcept<RangeType>));

        size_t count = 0;
        auto begin = boost::begin(range);
        auto total = boost::size(range);
        auto outBegin = out;
        
#pragma omp threadprivate(hasher)
#pragma omp parallel for
        for (size_t i = 0; i < total; i += 2)
        {
            if (i + 1 < total)
            {
                *out++ = (hasher << *(begin + i) << *(begin + i + 1)).final();
            }
            else
            {
                *out++ = (hasher << *(begin + i)).final();
            }

#pragma omp atomic
            ++count;
        }

        if (count > 1)
        {
            parseRange(std::make_tuple(outBegin, out), out,
                std::forward<bcos::crypto::Hashing<HashImpl>>(hasher));
        }
    }

private:
    std::vector<bcos::h256> m_nodes;
};
}  // namespace bcos::tool