#include "AnyHasher.h"

bcos::crypto::hasher::AnyHasher::AnyHasher(std::unique_ptr<AnyHasherInterface> anyHasher)
  : m_anyHasher(std::move(anyHasher))
{}

void bcos::crypto::hasher::AnyHasher::update(std::span<std::byte const> input)
{
    m_anyHasher->update(input);
}

void bcos::crypto::hasher::AnyHasher::final(std::span<std::byte> output)
{
    m_anyHasher->final(output);
}

bcos::crypto::hasher::AnyHasher bcos::crypto::hasher::AnyHasher::clone() const
{
    return {m_anyHasher->clone()};
}

size_t bcos::crypto::hasher::AnyHasher::hashSize() const
{
    return m_anyHasher->hashSize();
}
