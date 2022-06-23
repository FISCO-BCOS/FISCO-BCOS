#pragma once

#include "Merkle.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <boost/serialization/array.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>

namespace boost::serialization
{
template <class Archive, class T1, class T2, size_t T3>
void serialize(Archive& ar, bcos::tool::merkle::Merkle<T1, T2, T3>& merkle, [[maybe_unused]] unsigned int version)
{
    ar& merkle.m_nodes;
    ar& merkle.m_levels;
}

template <class Archive, class HashType>
void serialize(Archive& ar, bcos::tool::merkle::Proof<HashType>& proof, [[maybe_unused]] unsigned int version)
{
    ar& proof.hashes;
    ar& proof.levels;
}

}  // namespace boost::serialization

namespace std
{
template <class T1, class T2, size_t T3>
ostream& operator<<(ostream& stream, const bcos::tool::merkle::Merkle<T1, T2, T3>& merkle)
{
    auto range = std::ranges::subrange(merkle.m_nodes.begin(), merkle.m_nodes.begin());
    size_t level = 0;
    for (auto length : merkle.m_levels)
    {
        range = {std::end(range), std::end(range) + length};
        stream << "Level " << level;
        for (auto& hash : range)
        {
            stream << " " << hash;
        }
        stream << std::endl;
        ++level;
    }
    return stream;
}

template <class HashType>
ostream& operator<<(ostream& stream, const typename bcos::tool::merkle::Proof<HashType>& proof)
{
    auto range = std::ranges::subrange(proof.hashes.begin(), proof.hashes.begin());
    size_t level = 0;
    for (auto length : proof.levels)
    {
        range = {std::end(range), std::end(range) + length};
        stream << "Level " << level;
        for (auto& hash : range)
        {
            stream << " " << hash;
        }
        stream << std::endl;
        ++level;
    }
    return stream;
}
}  // namespace std