#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bcos::crypto::hasher
{
class Keccak256State
{
public:
    static constexpr size_t HASH_SIZE = 32;
    static constexpr size_t RATE = 136;

    Keccak256State() = default;

    void update(std::span<const std::byte> input);
    void final(std::span<std::byte> output);
    void reset() noexcept;

private:
    void absorb(std::span<const std::byte, RATE> block) noexcept;

    std::array<uint64_t, 25> m_state{};
    std::array<std::byte, RATE> m_buffer{};
    size_t m_buffered = 0;
};

void keccakF1600(std::array<uint64_t, 25>& state) noexcept;
}  // namespace bcos::crypto::hasher
