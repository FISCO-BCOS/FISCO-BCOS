#pragma once
#include "Hasher.h"
#include "bcos-crypto/TrivialObject.h"
#include <bcos-concepts/ByteBuffer.h>
#include <span>

namespace bcos::crypto::hasher
{
class AnyHasherInterface
{
public:
    AnyHasherInterface() = default;
    AnyHasherInterface(const AnyHasherInterface&) = default;
    AnyHasherInterface(AnyHasherInterface&&) = default;
    AnyHasherInterface& operator=(const AnyHasherInterface&) = default;
    AnyHasherInterface& operator=(AnyHasherInterface&&) = default;
    virtual ~AnyHasherInterface() = default;

    virtual void update(std::span<std::byte const> input) = 0;
    virtual void final(std::span<std::byte> output) = 0;
    virtual std::unique_ptr<AnyHasherInterface> clone() const = 0;
    virtual size_t hashSize() const = 0;
};

template <Hasher Hasher>
class AnyHasherImpl : public AnyHasherInterface
{
private:
    [[no_unique_address]] Hasher m_hasher;

public:
    AnyHasherImpl(Hasher hasher) : m_hasher(std::move(hasher)) {}
    void update(std::span<std::byte const> input) override { m_hasher.update(input); }
    void final(std::span<std::byte> output) override { m_hasher.final(output); }
    std::unique_ptr<AnyHasherInterface> clone() const override
    {
        return std::make_unique<AnyHasherImpl<Hasher>>(m_hasher.clone());
    }
    size_t hashSize() const override { return m_hasher.hashSize(); }
};

// Type erasure hasher
class AnyHasher
{
private:
    std::unique_ptr<AnyHasherInterface> m_anyHasher;

public:
    template <class Hasher>
    AnyHasher(Hasher hasher)
      : m_anyHasher(std::make_unique<AnyHasherImpl<Hasher>>(std::move(hasher)))
    {}
    AnyHasher(std::unique_ptr<AnyHasherInterface> anyHasher) : m_anyHasher(std::move(anyHasher)) {}
    AnyHasher(AnyHasher&&) noexcept = default;
    AnyHasher& operator=(AnyHasher&&) noexcept = default;
    AnyHasher(const AnyHasher&) = delete;
    AnyHasher& operator=(const AnyHasher&) = delete;
    ~AnyHasher() = default;

    void update(auto const& input)
    {
        auto view = bcos::crypto::trivial::toView(std::forward<decltype(input)>(input));
        m_anyHasher->update(view);
    }

    void final(concepts::bytebuffer::ByteBuffer auto& output)
    {
        concepts::resizeTo(output, hashSize());
        m_anyHasher->final(
            std::span<std::byte>((std::byte*)RANGES::data(output), RANGES::size(output)));
    }

    AnyHasher clone() const { return {m_anyHasher->clone()}; }
    size_t hashSize() const { return m_anyHasher->hashSize(); }
};

static_assert(Hasher<AnyHasher>, "Not a valid Hasher!");

}  // namespace bcos::crypto::hasher