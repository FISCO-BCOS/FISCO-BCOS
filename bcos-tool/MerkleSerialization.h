#pragma once

#include "Merkle.h"
#include <boost/serialization/serialization.hpp>
#include <iostream>

namespace boost::serialization
{
template <class Archive, bcos::crypto::hasher::Hasher HasherType, class HashType>
void serialize(Archive& ar, bcos::tool::merkle::Merkle<HasherType, HashType>& merkle,
    [[maybe_unused]] unsigned int version)
{
    ar& merkle.m_nodes;
    ar& merkle.m_levels;
}

template <class Archive, bcos::crypto::hasher::Hasher HasherType, class HashType>
void serialize(Archive& ar, typename bcos::tool::merkle::Merkle<HasherType, HashType>::Proof& proof,
    [[maybe_unused]] unsigned int version)
{
    ar& proof.hashes;
    ar& proof.levels;
}

}  // namespace boost::serialization

namespace std
{
template <bcos::crypto::hasher::Hasher HasherType, class HashType, size_t width = 2>
ostream& operator<<(
    ostream& stream, const bcos::tool::merkle::Merkle<HasherType, HashType, width>& merkle)
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

template <bcos::crypto::hasher::Hasher HasherType, class HashType, size_t width>
ostream& operator<<(ostream& stream,
    const typename bcos::tool::merkle::Merkle<HasherType, HashType, width>::Proof& proof)
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