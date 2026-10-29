#include "Keccak256.h"

#include <algorithm>
#include <stdexcept>

namespace bcos::crypto::hasher
{
namespace
{
constexpr std::array<uint64_t, 24> ROUND_CONSTANTS = {
    0x0000000000000001ULL,
    0x0000000000008082ULL,
    0x800000000000808aULL,
    0x8000000080008000ULL,
    0x000000000000808bULL,
    0x0000000080000001ULL,
    0x8000000080008081ULL,
    0x8000000000008009ULL,
    0x000000000000008aULL,
    0x0000000000000088ULL,
    0x0000000080008009ULL,
    0x000000008000000aULL,
    0x000000008000808bULL,
    0x800000000000008bULL,
    0x8000000000008089ULL,
    0x8000000000008003ULL,
    0x8000000000008002ULL,
    0x8000000000000080ULL,
    0x000000000000800aULL,
    0x800000008000000aULL,
    0x8000000080008081ULL,
    0x8000000000008080ULL,
    0x0000000080000001ULL,
    0x8000000080008008ULL,
};
constexpr std::array<int, 24> ROTATIONS = {
    1,
    3,
    6,
    10,
    15,
    21,
    28,
    36,
    45,
    55,
    2,
    14,
    27,
    41,
    56,
    8,
    25,
    43,
    62,
    18,
    39,
    61,
    20,
    44,
};
constexpr std::array<int, 24> PI_LANES = {
    10,
    7,
    11,
    17,
    18,
    3,
    5,
    16,
    8,
    21,
    24,
    4,
    15,
    23,
    19,
    13,
    12,
    2,
    20,
    14,
    22,
    9,
    6,
    1,
};

constexpr uint64_t rotateLeft(uint64_t value, int shift) noexcept
{
    return (value << shift) | (value >> (64 - shift));
}

uint64_t load64(const std::byte* data) noexcept
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        value |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    return value;
}
}  // namespace

void keccakF1600(std::array<uint64_t, 25>& state) noexcept
{
    for (uint64_t roundConstant : ROUND_CONSTANTS)
    {
        std::array<uint64_t, 5> column{};
        for (size_t x = 0; x < 5; ++x)
        {
            column[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (size_t x = 0; x < 5; ++x)
        {
            const uint64_t delta = column[(x + 4) % 5] ^ rotateLeft(column[(x + 1) % 5], 1);
            for (size_t y = 0; y < 5; ++y)
            {
                state[x + 5 * y] ^= delta;
            }
        }

        uint64_t current = state[1];
        for (size_t i = 0; i < PI_LANES.size(); ++i)
        {
            const int lane = PI_LANES[i];
            const uint64_t previous = state[lane];
            state[lane] = rotateLeft(current, ROTATIONS[i]);
            current = previous;
        }

        for (size_t y = 0; y < 5; ++y)
        {
            std::array<uint64_t, 5> row{};
            std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(5 * y), 5, row.begin());
            for (size_t x = 0; x < 5; ++x)
            {
                state[x + 5 * y] = row[x] ^ ((~row[(x + 1) % 5]) & row[(x + 2) % 5]);
            }
        }
        state[0] ^= roundConstant;
    }
}

void Keccak256State::absorb(std::span<const std::byte, RATE> block) noexcept
{
    for (size_t i = 0; i < RATE / 8; ++i)
    {
        m_state[i] ^= load64(block.data() + i * 8);
    }
    keccakF1600(m_state);
}

void Keccak256State::update(std::span<const std::byte> input)
{
    if (m_buffered != 0)
    {
        const size_t copied = std::min(input.size(), RATE - m_buffered);
        std::copy_n(input.begin(), static_cast<std::ptrdiff_t>(copied),
            m_buffer.begin() + static_cast<std::ptrdiff_t>(m_buffered));
        m_buffered += copied;
        input = input.subspan(copied);
        if (m_buffered == RATE)
        {
            absorb(std::span<const std::byte, RATE>(m_buffer.data(), RATE));
            m_buffered = 0;
        }
    }

    while (input.size() >= RATE)
    {
        absorb(std::span<const std::byte, RATE>(input.data(), RATE));
        input = input.subspan(RATE);
    }

    if (!input.empty())
    {
        std::copy(input.begin(), input.end(), m_buffer.begin());
        m_buffered = input.size();
    }
}

void Keccak256State::final(std::span<std::byte> output)
{
    if (output.size() < HASH_SIZE)
    {
        throw std::invalid_argument("Keccak-256 output buffer too small");
    }

    std::array<std::byte, RATE> padding{};
    std::copy_n(m_buffer.begin(), static_cast<std::ptrdiff_t>(m_buffered), padding.begin());
    padding[m_buffered] = std::byte{0x01};
    padding.back() |= std::byte{0x80};
    absorb(std::span<const std::byte, RATE>(padding.data(), RATE));

    for (size_t i = 0; i < HASH_SIZE; ++i)
    {
        output[i] = static_cast<std::byte>((m_state[i / 8] >> (8 * (i % 8))) & 0xff);
    }
    reset();
}

void Keccak256State::reset() noexcept
{
    m_state.fill(0);
    m_buffer.fill(std::byte{0});
    m_buffered = 0;
}
}  // namespace bcos::crypto::hasher
