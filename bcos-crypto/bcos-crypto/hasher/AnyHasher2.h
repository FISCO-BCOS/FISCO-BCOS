#pragma once
#include "Hasher.h"
#include <bcos-concepts/ByteBuffer.h>
#include <span>

namespace bcos::crypto::hasher
{
class AnyHasher2Interface
{
public:
    AnyHasher2Interface() = default;
    AnyHasher2Interface(const AnyHasher2Interface&) = default;
    AnyHasher2Interface(AnyHasher2Interface&&) = default;
    AnyHasher2Interface& operator=(const AnyHasher2Interface&) = default;
    AnyHasher2Interface& operator=(AnyHasher2Interface&&) = default;
    virtual ~AnyHasher2Interface() = default;

    virtual void update(std::span<std::byte const> input) = 0;
    virtual void final(std::span<std::byte> output) = 0;
    virtual std::unique_ptr<AnyHasher2Interface> clone() const = 0;
};

template <Hasher Hasher>
class AnyHasher2Impl : public AnyHasher2Interface
{
private:
    [[no_unique_address]] Hasher m_hasher;

public:
    void update(std::span<std::byte const> input) override { m_hasher.update(input); }
    void final(std::span<std::byte> output) override { m_hasher.final(output); }
    std::unique_ptr<AnyHasher2Interface> clone() const override
    {
        return std::make_unique<AnyHasher2Impl<Hasher>>();
    }
};

// True type erasure hasher
class AnyHasher2
{
private:
    constexpr static size_t EMPLACE_SIZE = 24;
    std::unique_ptr<AnyHasher2Interface> m_anyHasher;

public:
    AnyHasher2(std::unique_ptr<AnyHasher2Interface> anyHasher) : m_anyHasher(std::move(anyHasher))
    {}

    void update(concepts::bytebuffer::ByteBuffer auto const& input)
    {
        m_anyHasher->update(
            std::span<std::byte const>((const std::byte*)RANGES::data(input), RANGES::size(input)));
    }

    void final(concepts::bytebuffer::ByteBuffer auto& output)
    {
        m_anyHasher->final(
            std::span<std::byte>((std::byte*)RANGES::data(output), RANGES::size(output)));
    }

    AnyHasher2 clone() const { return {m_anyHasher->clone()}; }
};

}  // namespace bcos::crypto::hasher