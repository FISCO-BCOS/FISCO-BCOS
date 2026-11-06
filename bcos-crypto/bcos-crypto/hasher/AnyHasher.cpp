#include "AnyHasher.h"

void bcos::crypto::hasher::AnyHasher::update(std::span<std::byte const> input)
{
    m_anyHasher->update(input);
}

void bcos::crypto::hasher::AnyHasher::final(std::span<std::byte> output)
{
    m_anyHasher->final(output);
}

void bcos::crypto::hasher::AnyHasher::digest(std::span<std::byte> output)
{
    m_anyHasher->digest(output);
}

bcos::crypto::hasher::AnyHasher bcos::crypto::hasher::AnyHasher::clone() const
{
    return m_anyHasher->clone();
}

size_t bcos::crypto::hasher::AnyHasher::hashSize() const
{
    return m_anyHasher->hashSize();
}
