// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "precompiles.hpp"
#include "../utils/stdx/utility.hpp"
#include "evmone_precompiles/secp256r1.hpp"
#include "precompiles_internal.hpp"
#include <evmone_precompiles/blake2b.hpp>
#include <evmone_precompiles/bls.hpp>
#include <evmone_precompiles/bn254.hpp>
#include <evmone_precompiles/kzg.hpp>
#include <evmone_precompiles/modexp.hpp>
#include <evmone_precompiles/ripemd160.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <evmone_precompiles/sha256.hpp>
#include <intx/intx.hpp>
#include <array>
#include <bit>
#include <cassert>
#include <limits>
#include <span>

#ifdef EVMONE_PRECOMPILES_LIBSECP256K1
#include "precompiles_libsecp256k1.hpp"
#endif

#ifdef EVMONE_PRECOMPILES_GMP
#include "precompiles_gmp.hpp"
#endif

namespace evmone::state
{
using evmc::bytes;
using evmc::bytes_view;
using namespace evmc::literals;

namespace
{
constexpr auto GasCostMax = std::numeric_limits<int64_t>::max();

constexpr auto MODEXP_LEN_LIMIT_EIP7823 = 1024;

constexpr auto BLS12_SCALAR_SIZE = 32;
constexpr auto BLS12_FIELD_ELEMENT_SIZE = 64;
constexpr auto BLS12_G1_POINT_SIZE = 2 * BLS12_FIELD_ELEMENT_SIZE;
constexpr auto BLS12_G2_POINT_SIZE = 4 * BLS12_FIELD_ELEMENT_SIZE;
constexpr auto BLS12_G1_MUL_INPUT_SIZE = BLS12_G1_POINT_SIZE + BLS12_SCALAR_SIZE;
constexpr auto BLS12_G2_MUL_INPUT_SIZE = BLS12_G2_POINT_SIZE + BLS12_SCALAR_SIZE;

constexpr int64_t num_words(size_t size_in_bytes) noexcept
{
    return static_cast<int64_t>((size_in_bytes + 31) / 32);
}

template <int BaseCost, int WordCost>
constexpr int64_t cost_per_input_word(size_t input_size) noexcept
{
    return BaseCost + WordCost * num_words(input_size);
}
}  // namespace

PrecompileAnalysis ecrecover_analyze(bytes_view /*input*/, evmc_revision /*rev*/) noexcept
{
    return {3000, 32};
}

PrecompileAnalysis sha256_analyze(bytes_view input, evmc_revision /*rev*/) noexcept
{
    return {cost_per_input_word<60, 12>(input.size()), 32};
}

PrecompileAnalysis ripemd160_analyze(bytes_view input, evmc_revision /*rev*/) noexcept
{
    return {cost_per_input_word<600, 120>(input.size()), 32};
}

PrecompileAnalysis identity_analyze(bytes_view input, evmc_revision /*rev*/) noexcept
{
    return {cost_per_input_word<15, 3>(input.size()), input.size()};
}

PrecompileAnalysis ecadd_analyze(bytes_view /*input*/, evmc_revision rev) noexcept
{
    return {rev >= EVMC_ISTANBUL ? 150 : 500, 64};
}

PrecompileAnalysis ecmul_analyze(bytes_view /*input*/, evmc_revision rev) noexcept
{
    return {rev >= EVMC_ISTANBUL ? 6000 : 40000, 64};
}

PrecompileAnalysis ecpairing_analyze(bytes_view input, evmc_revision rev) noexcept
{
    const auto base_cost = (rev >= EVMC_ISTANBUL) ? 45000 : 100000;
    const auto element_cost = (rev >= EVMC_ISTANBUL) ? 34000 : 80000;
    const auto num_elements = static_cast<int64_t>(input.size() / 192);
    return {base_cost + num_elements * element_cost, 32};
}

PrecompileAnalysis blake2bf_analyze(bytes_view input, evmc_revision) noexcept
{
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    return {input.size() == 213 ? intx::be::unsafe::load<uint32_t>(input.data()) : GasCostMax, 64};
}

PrecompileAnalysis expmod_analyze(bytes_view input, evmc_revision rev) noexcept
{
    using namespace intx;

    const auto calc_adjusted_exp_len = [input, rev](size_t offset, uint32_t len) noexcept {
        const auto head_len = std::min(size_t{len}, size_t{32});
        const auto head_explicit_bytes =
            offset < input.size() ?
                input.substr(offset, std::min(head_len, input.size() - offset)) :
                bytes_view{};

        const auto top_byte_index = head_explicit_bytes.find_first_not_of(uint8_t{0});
        const auto exp_bit_width =
            (top_byte_index != bytes_view::npos) ?
                8 * (head_len - top_byte_index - 1) +
                    static_cast<unsigned>(std::bit_width(head_explicit_bytes[top_byte_index])) :
                0;

        const auto tail_len = len - head_len;
        const auto head_bits = std::max(exp_bit_width, size_t{1}) - 1;
        const uint64_t factor = rev < EVMC_OSAKA ? 8 : 16;
        return std::max(factor * uint64_t{tail_len} + uint64_t{head_bits}, uint64_t{1});
    };

    static constexpr auto calc_mult_complexity_eip7883 = [](uint32_t max_len) noexcept {
        // With EIP-7823 the computation never overflows.
        assert(max_len <= MODEXP_LEN_LIMIT_EIP7823);
        const auto num_words = (max_len + 7) / 8;
        const auto mult_complexity = max_len <= 32 ? 16 : num_words * num_words * 2;
        return uint64_t{mult_complexity};
    };
    static constexpr auto calc_mult_complexity_eip2565 = [](uint32_t max_len) noexcept {
        const auto num_words = (uint64_t{max_len} + 7) / 8;
        return num_words * num_words;  // max value: 0x04000000'00000000
    };
    static constexpr auto calc_mult_complexity_eip198 = [](uint32_t max_len) noexcept {
        const auto max_len_squared = uint64_t{max_len} * max_len;
        if (max_len <= 64)
            return max_len_squared;
        if (max_len <= 1024)
            return max_len_squared / 4 + 96 * max_len - 3072;
        // max value: 0x100001df'dffcf220
        return max_len_squared / 16 + 480 * uint64_t{max_len} - 199680;
    };

    struct Params
    {
        int64_t min_gas;
        unsigned final_divisor;
        uint64_t (*calc_mult_complexity)(uint32_t max_len) noexcept;
    };
    const auto& [min_gas, final_divisor, calc_mult_complexity] = [rev]() noexcept -> Params {
        if (rev >= EVMC_OSAKA)
            return {500, 1, calc_mult_complexity_eip7883};
        else if (rev >= EVMC_BERLIN)
            return {200, 3, calc_mult_complexity_eip2565};
        else  // Byzantium
            return {0, 20, calc_mult_complexity_eip198};
    }();

    static constexpr size_t INPUT_HEADER_REQUIRED_SIZE = 3 * sizeof(uint256);
    uint8_t input_header[INPUT_HEADER_REQUIRED_SIZE]{};
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    std::copy_n(input.data(), std::min(input.size(), INPUT_HEADER_REQUIRED_SIZE), input_header);

    const auto base_len256 = be::unsafe::load<uint256>(&input_header[0]);
    const auto exp_len256 = be::unsafe::load<uint256>(&input_header[32]);
    const auto mod_len256 = be::unsafe::load<uint256>(&input_header[64]);

    // Check the declared input lengths against the practical (2**32)
    // or specified (EIP-7823: 2**10) limits.
    const auto len_limit =
        rev < EVMC_OSAKA ? std::numeric_limits<uint32_t>::max() : MODEXP_LEN_LIMIT_EIP7823;
    if (base_len256 > len_limit || mod_len256 > len_limit)
        return {GasCostMax, 0};

    if (exp_len256 > len_limit)
    {
        // Before EIP-7823, the big exponent may be canceled with zero multiplication complexity.
        if (rev < EVMC_OSAKA && base_len256 == 0 && mod_len256 == 0)
            return {min_gas, 0};
        return {GasCostMax, 0};
    }

    const auto base_len = static_cast<uint32_t>(base_len256);
    const auto exp_len = static_cast<uint32_t>(exp_len256);
    const auto mod_len = static_cast<uint32_t>(mod_len256);

    const auto adjusted_exp_len = calc_adjusted_exp_len(sizeof(input_header) + base_len, exp_len);
    const auto max_len = std::max(mod_len, base_len);
    const auto gas = umul(calc_mult_complexity(max_len), adjusted_exp_len) / final_divisor;
    const auto gas_clamped = std::clamp<uint128>(gas, min_gas, GasCostMax);
    return {static_cast<int64_t>(gas_clamped), mod_len};
}

PrecompileAnalysis point_evaluation_analyze(bytes_view, evmc_revision) noexcept
{
    static constexpr auto POINT_EVALUATION_PRECOMPILE_GAS = 50000;
    return {POINT_EVALUATION_PRECOMPILE_GAS, 64};
}

PrecompileAnalysis bls12_g1add_analyze(bytes_view, evmc_revision) noexcept
{
    static constexpr auto BLS12_G1ADD_PRECOMPILE_GAS = 375;
    return {BLS12_G1ADD_PRECOMPILE_GAS, BLS12_G1_POINT_SIZE};
}

PrecompileAnalysis bls12_g1msm_analyze(bytes_view input, evmc_revision) noexcept
{
    static constexpr auto G1MUL_GAS_COST = 12000;
    static constexpr uint16_t DISCOUNTS[] = {1000, 949, 848, 797, 764, 750, 738, 728, 719, 712, 705,
        698, 692, 687, 682, 677, 673, 669, 665, 661, 658, 654, 651, 648, 645, 642, 640, 637, 635,
        632, 630, 627, 625, 623, 621, 619, 617, 615, 613, 611, 609, 608, 606, 604, 603, 601, 599,
        598, 596, 595, 593, 592, 591, 589, 588, 586, 585, 584, 582, 581, 580, 579, 577, 576, 575,
        574, 573, 572, 570, 569, 568, 567, 566, 565, 564, 563, 562, 561, 560, 559, 558, 557, 556,
        555, 554, 553, 552, 551, 550, 549, 548, 547, 547, 546, 545, 544, 543, 542, 541, 540, 540,
        539, 538, 537, 536, 536, 535, 534, 533, 532, 532, 531, 530, 529, 528, 528, 527, 526, 525,
        525, 524, 523, 522, 522, 521, 520, 520, 519};

    if (input.empty() || input.size() % BLS12_G1_MUL_INPUT_SIZE != 0)
        return {GasCostMax, 0};

    const auto k = input.size() / BLS12_G1_MUL_INPUT_SIZE;
    assert(k > 0);
    const auto discount = DISCOUNTS[std::min(k, std::size(DISCOUNTS)) - 1];
    const auto cost = (G1MUL_GAS_COST * discount * static_cast<int64_t>(k)) / 1000;
    return {cost, BLS12_G1_POINT_SIZE};
}

PrecompileAnalysis bls12_g2add_analyze(bytes_view, evmc_revision) noexcept
{
    static constexpr auto BLS12_G2ADD_PRECOMPILE_GAS = 600;
    return {BLS12_G2ADD_PRECOMPILE_GAS, BLS12_G2_POINT_SIZE};
}

PrecompileAnalysis bls12_g2msm_analyze(bytes_view input, evmc_revision) noexcept
{
    static constexpr auto G2MUL_GAS_COST = 22500;
    static constexpr uint16_t DISCOUNTS[] = {1000, 1000, 923, 884, 855, 832, 812, 796, 782, 770,
        759, 749, 740, 732, 724, 717, 711, 704, 699, 693, 688, 683, 679, 674, 670, 666, 663, 659,
        655, 652, 649, 646, 643, 640, 637, 634, 632, 629, 627, 624, 622, 620, 618, 615, 613, 611,
        609, 607, 606, 604, 602, 600, 598, 597, 595, 593, 592, 590, 589, 587, 586, 584, 583, 582,
        580, 579, 578, 576, 575, 574, 573, 571, 570, 569, 568, 567, 566, 565, 563, 562, 561, 560,
        559, 558, 557, 556, 555, 554, 553, 552, 552, 551, 550, 549, 548, 547, 546, 545, 545, 544,
        543, 542, 541, 541, 540, 539, 538, 537, 537, 536, 535, 535, 534, 533, 532, 532, 531, 530,
        530, 529, 528, 528, 527, 526, 526, 525, 524, 524};

    if (input.empty() || input.size() % BLS12_G2_MUL_INPUT_SIZE != 0)
        return {GasCostMax, 0};

    const auto k = input.size() / BLS12_G2_MUL_INPUT_SIZE;
    assert(k > 0);
    const auto discount = DISCOUNTS[std::min(k, std::size(DISCOUNTS)) - 1];
    const auto cost = (G2MUL_GAS_COST * discount * static_cast<int64_t>(k)) / 1000;
    return {cost, BLS12_G2_POINT_SIZE};
}

PrecompileAnalysis bls12_pairing_check_analyze(bytes_view input, evmc_revision) noexcept
{
    static constexpr auto PAIR_SIZE = BLS12_G1_POINT_SIZE + BLS12_G2_POINT_SIZE;

    if (input.empty() || input.size() % PAIR_SIZE != 0)
        return {GasCostMax, 0};

    const auto npairs = static_cast<int64_t>(input.size()) / PAIR_SIZE;

    static constexpr auto BLS12_PAIRING_CHECK_BASE_FEE_PRECOMPILE_GAS = 37700;
    static constexpr auto BLS12_PAIRING_CHECK_FEE_PRECOMPILE_GAS = 32600;
    return {BLS12_PAIRING_CHECK_BASE_FEE_PRECOMPILE_GAS +
                BLS12_PAIRING_CHECK_FEE_PRECOMPILE_GAS * npairs,
        32};
}

PrecompileAnalysis bls12_map_fp_to_g1_analyze(bytes_view, evmc_revision) noexcept
{
    static constexpr auto BLS12_MAP_FP_TO_G1_PRECOMPILE_GAS = 5500;
    return {BLS12_MAP_FP_TO_G1_PRECOMPILE_GAS, BLS12_G1_POINT_SIZE};
}

PrecompileAnalysis bls12_map_fp2_to_g2_analyze(bytes_view, evmc_revision) noexcept
{
    static constexpr auto BLS12_MAP_FP2_TO_G2_PRECOMPILE_GAS = 23800;
    return {BLS12_MAP_FP2_TO_G2_PRECOMPILE_GAS, BLS12_G2_POINT_SIZE};
}

PrecompileAnalysis p256verify_analyze(bytes_view, evmc_revision) noexcept
{
    return {6900, 32};
}

namespace
{
class EcrecoverInput
{
    uint8_t buffer_[128]{};

public:
    explicit EcrecoverInput(std::span<const uint8_t> input) noexcept
    {
        std::copy_n(input.data(), std::min(input.size(), std::size(buffer_)), buffer_);
    }

    std::optional<std::tuple<std::span<const uint8_t, 32>, std::span<const uint8_t, 64>, bool>>
    parse() const noexcept
    {
        const auto hash = std::span{buffer_}.subspan<0, 32>();
        const auto v_bytes = std::span{buffer_}.subspan<32, 32>();
        const auto sig_bytes = std::span{buffer_}.subspan<64, 64>();

        const auto v = intx::be::unsafe::load<intx::uint256>(v_bytes.data());
        if (v != 27 && v != 28)
            return std::nullopt;
        const bool parity = v == 28;

        return std::tuple{hash, sig_bytes, parity};
    }
};

}  // namespace

ExecutionResult ecrecover_execute_evmone(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    const EcrecoverInput input_buffer{std::span{input, input_size}};
    const auto o = input_buffer.parse();
    if (!o)
        return {EVMC_SUCCESS, 0};
    const auto& [hash, sig_bytes, parity] = *o;

    const auto res = evmmax::secp256k1::ecrecover(
        hash, sig_bytes.subspan<0, 32>(), sig_bytes.subspan<32, 32>(), parity);
    if (!res)
        return {EVMC_SUCCESS, 0};

    const auto it = std::fill_n(output, 32 - sizeof(*res), 0);
    std::copy_n(res->bytes, sizeof(*res), it);
    return {EVMC_SUCCESS, 32};
}

#ifdef EVMONE_PRECOMPILES_LIBSECP256K1
ExecutionResult ecrecover_execute_libsecp256k1(const uint8_t* input, size_t input_size,
    uint8_t* output, [[maybe_unused]] size_t output_size) noexcept
{
    const EcrecoverInput input_buffer{std::span{input, input_size}};
    const auto o = input_buffer.parse();
    if (!o)
        return {EVMC_SUCCESS, 0};
    const auto& [hash, sig_bytes, parity] = *o;

    uint8_t pubkey[64];
    if (!ecrecover_libsecp256k1(pubkey, hash, sig_bytes, parity))
        return {EVMC_SUCCESS, 0};

    const auto addr = evmmax::secp256k1::to_address(pubkey);
    const auto it = std::fill_n(output, 32 - sizeof(addr), 0);
    std::copy_n(addr.bytes, sizeof(addr), it);
    return {EVMC_SUCCESS, 32};
}
#endif

ExecutionResult ecrecover_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept
{
    assert(output_size >= 32);
// Select better implementation.
#ifdef EVMONE_PRECOMPILES_LIBSECP256K1
    return ecrecover_execute_libsecp256k1(input, input_size, output, output_size);
#else
    return ecrecover_execute_evmone(input, input_size, output, output_size);
#endif
}

ExecutionResult sha256_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 32);
    crypto::sha256(reinterpret_cast<std::byte*>(output), reinterpret_cast<const std::byte*>(input),
        input_size);
    return {EVMC_SUCCESS, 32};
}

ExecutionResult ripemd160_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 32);
    output = std::fill_n(output, 12, std::uint8_t{0});
    crypto::ripemd160(reinterpret_cast<std::byte*>(output),
        reinterpret_cast<const std::byte*>(input), input_size);
    return {EVMC_SUCCESS, 32};
}

static std::tuple<std::span<const uint8_t>, std::span<const uint8_t>, std::span<const uint8_t>>
expmod_parse_input(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept
{
    static constexpr auto LEN_SIZE = sizeof(intx::uint256);
    static constexpr auto HEADER_SIZE = 3 * LEN_SIZE;
    static constexpr auto LEN32_OFF = LEN_SIZE - sizeof(uint32_t);

    // The output size equal to the modulus size.
    const auto mod_len = output_size;

    // Handle short incomplete input up front. The answer is 0 of the length of the modulus.
    if (input_size <= HEADER_SIZE) [[unlikely]]
    {
        std::fill_n(output, output_size, 0);
        return {};
    }

    const auto base_len = intx::be::unsafe::load<uint32_t>(&input[LEN32_OFF]);
    const auto exp_len = intx::be::unsafe::load<uint32_t>(&input[LEN_SIZE + LEN32_OFF]);
    assert(intx::be::unsafe::load<uint32_t>(&input[2 * LEN_SIZE + LEN32_OFF]) == mod_len);

    const size_t mod_off = base_len + exp_len;  // Cannot overflow if gas cost computed before.
    const size_t payload_max_size = mod_off + mod_len;  // Input may contain extra bytes.
    const std::span payload{
        input + HEADER_SIZE, std::min(input_size - HEADER_SIZE, payload_max_size)};
    const auto mod_explicit = payload.subspan(std::min(mod_off, payload.size()));

    // Handle the mod being zero early.
    // This serves two purposes:
    // - bigint libraries don't like such a modulus because division by 0 is not well-defined,
    // - having non-zero modulus guarantees that base and exp aren't out-of-bounds.
    if (std::ranges::all_of(mod_explicit, [](uint8_t b) { return b == 0; })) [[unlikely]]
    {
        // The modulus is zero, so the result is zero.
        std::fill_n(output, output_size, 0);
        return {};
    }

    const auto mod_requires_padding = mod_explicit.size() != mod_len;
    if (mod_requires_padding) [[unlikely]]
    {
        // The modulus is the last argument, and some of its bytes may be missing and be implicitly
        // zero. In this case, copy the explicit modulus bytes to the output buffer and pad the rest
        // with zeroes. The output buffer is guaranteed to have exactly the modulus size.
        const auto [_, output_p] = std::ranges::copy(mod_explicit, output);
        std::fill(output_p, output + output_size, 0);
    }

    const auto base = payload.subspan(0, base_len);
    const auto exp = payload.subspan(base_len, exp_len);
    const auto mod = mod_requires_padding ? std::span{output, mod_len} : mod_explicit;

    return {base, exp, mod};
}

ExecutionResult expmod_execute_evmone(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept
{
    const auto [base, exp, mod] = expmod_parse_input(input, input_size, output, output_size);
    if (mod.empty())
        return {EVMC_SUCCESS, output_size};

    crypto::modexp(base, exp, mod, output);
    return {EVMC_SUCCESS, output_size};
}

#ifdef EVMONE_PRECOMPILES_GMP
ExecutionResult expmod_execute_gmp(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept
{
    const auto [base, exp, mod] = expmod_parse_input(input, input_size, output, output_size);
    if (mod.empty())
        return {EVMC_SUCCESS, output_size};

    expmod_gmp(base, exp, mod, output);
    return {EVMC_SUCCESS, output_size};
}
#endif

ExecutionResult expmod_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept
{
#ifdef EVMONE_PRECOMPILES_GMP
    return expmod_execute_gmp(input, input_size, output, output_size);
#else
    return expmod_execute_evmone(input, input_size, output, output_size);
#endif
}

ExecutionResult ecadd_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 64);

    uint8_t input_buffer[128]{};
    if (input_size != 0)
        std::memcpy(input_buffer, input, std::min(input_size, std::size(input_buffer)));

    const auto input_span = std::span{input_buffer};

    using namespace evmmax::bn254;

    const auto p = AffinePoint::from_bytes(input_span.subspan<0, 64>());
    const auto q = AffinePoint::from_bytes(input_span.subspan<64, 64>());
    if (!p.has_value() || !q.has_value()) [[unlikely]]
        return {EVMC_PRECOMPILE_FAILURE, 0};
    if (!validate(*p) || !validate(*q)) [[unlikely]]
        return {EVMC_PRECOMPILE_FAILURE, 0};

    const auto res = evmmax::ecc::add_affine(*p, *q);
    const std::span<uint8_t, 64> output_span{output, 64};
    res.to_bytes(output_span);
    return {EVMC_SUCCESS, output_span.size()};
}

ExecutionResult ecmul_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 64);

    uint8_t input_buffer[96]{};
    if (input_size != 0)
        std::memcpy(input_buffer, input, std::min(input_size, std::size(input_buffer)));

    const auto input_span = std::span{input_buffer};

    using namespace evmmax::bn254;

    const auto p = AffinePoint::from_bytes(input_span.subspan<0, 64>());
    if (!p.has_value() || !validate(*p)) [[unlikely]]
        return {EVMC_PRECOMPILE_FAILURE, 0};

    const auto c = intx::be::unsafe::load<uint256>(input_buffer + 64);

    const auto res = evmmax::bn254::mul(*p, c);
    const std::span<uint8_t, 64> output_span{output, 64};
    res.to_bytes(output_span);
    return {EVMC_SUCCESS, output_span.size()};
}

ExecutionResult ecpairing_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    static constexpr auto OUTPUT_SIZE = 32;
    static constexpr size_t PAIR_SIZE = 192;
    assert(output_size >= OUTPUT_SIZE);

    if (input_size % PAIR_SIZE != 0)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    std::vector<std::pair<evmmax::bn254::Point, evmmax::bn254::ExtPoint>> pairs;
    pairs.reserve(input_size / PAIR_SIZE);  // TODO: may throw std::bad_alloc.
    for (auto input_ptr = input; input_ptr != input + input_size; input_ptr += PAIR_SIZE)
    {
        const evmmax::bn254::Point p{
            intx::be::unsafe::load<intx::uint256>(input_ptr),
            intx::be::unsafe::load<intx::uint256>(input_ptr + 32),
        };
        const evmmax::bn254::ExtPoint q{
            {intx::be::unsafe::load<intx::uint256>(input_ptr + 96),
                intx::be::unsafe::load<intx::uint256>(input_ptr + 64)},
            {intx::be::unsafe::load<intx::uint256>(input_ptr + 160),
                intx::be::unsafe::load<intx::uint256>(input_ptr + 128)},
        };
        pairs.emplace_back(p, q);
    }

    const auto res = evmmax::bn254::pairing_check(pairs);
    if (!res.has_value())
        return {EVMC_PRECOMPILE_FAILURE, 0};

    std::fill_n(output, OUTPUT_SIZE, 0);
    output[OUTPUT_SIZE - 1] = *res ? 1 : 0;
    return {EVMC_SUCCESS, OUTPUT_SIZE};
}

ExecutionResult identity_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= input_size);
    std::copy_n(input, input_size, output);
    return {EVMC_SUCCESS, input_size};
}

ExecutionResult blake2bf_execute(const uint8_t* input, [[maybe_unused]] size_t input_size,
    uint8_t* output, [[maybe_unused]] size_t output_size) noexcept
{
    static_assert(std::endian::native == std::endian::little,
        "blake2bf only works correctly on little-endian architectures");
    assert(input_size >= 213);
    assert(output_size >= 64);

    const auto rounds = intx::be::unsafe::load<uint32_t>(input);
    input += sizeof(rounds);

    uint64_t h[8];
    std::memcpy(h, input, sizeof(h));
    input += sizeof(h);

    uint64_t m[16];
    std::memcpy(m, input, sizeof(m));
    input += sizeof(m);

    uint64_t t[2];
    std::memcpy(t, input, sizeof(t));
    input += sizeof(t);

    const auto f = *input;
    if (f != 0 && f != 1) [[unlikely]]
        return {EVMC_PRECOMPILE_FAILURE, 0};

    crypto::blake2b_compress(rounds, h, m, t, f != 0);
    std::memcpy(output, h, sizeof(h));
    return {EVMC_SUCCESS, sizeof(h)};
}

ExecutionResult point_evaluation_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 64);
    if (input_size != 192)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    const auto r = crypto::kzg_verify_proof(reinterpret_cast<const std::byte*>(&input[0]),
        reinterpret_cast<const std::byte*>(&input[32]),
        reinterpret_cast<const std::byte*>(&input[64]),
        reinterpret_cast<const std::byte*>(&input[96]),
        reinterpret_cast<const std::byte*>(&input[96 + 48]));

    if (!r)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    // Return FIELD_ELEMENTS_PER_BLOB and BLS_MODULUS as padded 32 byte big endian values
    // as required by the EIP-4844.
    intx::be::unsafe::store(output, crypto::FIELD_ELEMENTS_PER_BLOB);
    intx::be::unsafe::store(output + 32, crypto::BLS_MODULUS);
    return {EVMC_SUCCESS, 64};
}

ExecutionResult bls12_g1add_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    if (input_size != 2 * BLS12_G1_POINT_SIZE)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    assert(output_size == BLS12_G1_POINT_SIZE);

    if (!crypto::bls::g1_add(output, &output[64], input, &input[64], &input[128], &input[192]))
        return {EVMC_PRECOMPILE_FAILURE, 0};

    return {EVMC_SUCCESS, BLS12_G1_POINT_SIZE};
}

ExecutionResult bls12_g1msm_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    // Checked in `_analyze` function which must be called before.
    assert(input_size % BLS12_G1_MUL_INPUT_SIZE == 0);
    assert(output_size == BLS12_G1_POINT_SIZE);

    if (input_size == BLS12_G1_MUL_INPUT_SIZE)
    {
        // Optimize single multiplication case.
        if (!crypto::bls::g1_mul(output, &output[64], input, &input[64], &input[128]))
            return {EVMC_PRECOMPILE_FAILURE, 0};
    }
    else
    {
        // TODO: g1_msm() may throw std::bad_alloc.
        if (!crypto::bls::g1_msm(output, &output[64], input, input_size))
            return {EVMC_PRECOMPILE_FAILURE, 0};
    }

    return {EVMC_SUCCESS, BLS12_G1_POINT_SIZE};
}

ExecutionResult bls12_g2add_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    if (input_size != 2 * BLS12_G2_POINT_SIZE)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    assert(output_size == BLS12_G2_POINT_SIZE);

    if (!crypto::bls::g2_add(output, &output[128], input, &input[128], &input[256], &input[384]))
        return {EVMC_PRECOMPILE_FAILURE, 0};

    return {EVMC_SUCCESS, BLS12_G2_POINT_SIZE};
}

ExecutionResult bls12_g2msm_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    // Checked in `_analyze` function which must be called before.
    assert(input_size % BLS12_G2_MUL_INPUT_SIZE == 0);
    assert(output_size == BLS12_G2_POINT_SIZE);

    if (input_size == BLS12_G2_MUL_INPUT_SIZE)
    {
        // Optimize single multiplication case.
        if (!crypto::bls::g2_mul(output, &output[128], input, &input[128], &input[256]))
            return {EVMC_PRECOMPILE_FAILURE, 0};
    }
    else
    {
        // TODO: g2_msm() may throw std::bad_alloc.
        if (!crypto::bls::g2_msm(output, &output[128], input, input_size))
            return {EVMC_PRECOMPILE_FAILURE, 0};
    }

    return {EVMC_SUCCESS, BLS12_G2_POINT_SIZE};
}

ExecutionResult bls12_pairing_check_execute(const uint8_t* input, size_t input_size,
    uint8_t* output, [[maybe_unused]] size_t output_size) noexcept
{
    // Checked in `_analyze` function which must be called before.
    assert(input_size % (BLS12_G1_POINT_SIZE + BLS12_G2_POINT_SIZE) == 0);
    assert(output_size == 32);

    if (!crypto::bls::pairing_check(output, input, input_size))
        return {EVMC_PRECOMPILE_FAILURE, 0};

    return {EVMC_SUCCESS, 32};
}

ExecutionResult bls12_map_fp_to_g1_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    if (input_size != BLS12_FIELD_ELEMENT_SIZE)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    assert(output_size == BLS12_G1_POINT_SIZE);

    if (!crypto::bls::map_fp_to_g1(output, &output[64], input))
        return {EVMC_PRECOMPILE_FAILURE, 0};

    return {EVMC_SUCCESS, BLS12_G1_POINT_SIZE};
}

ExecutionResult bls12_map_fp2_to_g2_execute(const uint8_t* input, size_t input_size,
    uint8_t* output, [[maybe_unused]] size_t output_size) noexcept
{
    if (input_size != 2 * BLS12_FIELD_ELEMENT_SIZE)
        return {EVMC_PRECOMPILE_FAILURE, 0};

    assert(output_size == BLS12_G2_POINT_SIZE);

    if (!crypto::bls::map_fp2_to_g2(output, &output[128], input))
        return {EVMC_PRECOMPILE_FAILURE, 0};

    return {EVMC_SUCCESS, BLS12_G2_POINT_SIZE};
}

ExecutionResult p256verify_execute(const uint8_t* input, size_t input_size, uint8_t* output,
    [[maybe_unused]] size_t output_size) noexcept
{
    assert(output_size >= 32);

    if (input_size != 160)
        return {EVMC_SUCCESS, 0};

    ethash::hash256 h{};
    std::copy_n(input, sizeof(h), h.bytes);
    const auto r = intx::be::unsafe::load<intx::uint256>(input + 32);
    const auto s = intx::be::unsafe::load<intx::uint256>(input + 64);
    const auto qx = intx::be::unsafe::load<intx::uint256>(input + 96);
    const auto qy = intx::be::unsafe::load<intx::uint256>(input + 128);

    if (!evmmax::secp256r1::verify(h, r, s, qx, qy))
        return {EVMC_SUCCESS, 0};  // In case of invalid signature, return empty output.

    // Return 1_u256.
    std::fill_n(output, 31, 0);
    output[31] = 1;
    return {EVMC_SUCCESS, 32};
}

namespace
{
using PrecompileLookupIndex = uint16_t;

struct PrecompileTraits
{
    PrecompileLookupIndex address = 0;
    evmc_revision since = EVMC_FRONTIER;
    decltype(identity_analyze)* analyze = nullptr;
    decltype(identity_execute)* execute = nullptr;
};

inline constexpr std::array<PrecompileTraits, 18> traits{{
    {0x0001, EVMC_FRONTIER, ecrecover_analyze, ecrecover_execute},
    {0x0002, EVMC_FRONTIER, sha256_analyze, sha256_execute},
    {0x0003, EVMC_FRONTIER, ripemd160_analyze, ripemd160_execute},
    {0x0004, EVMC_FRONTIER, identity_analyze, identity_execute},
    {0x0005, EVMC_BYZANTIUM, expmod_analyze, expmod_execute},
    {0x0006, EVMC_BYZANTIUM, ecadd_analyze, ecadd_execute},
    {0x0007, EVMC_BYZANTIUM, ecmul_analyze, ecmul_execute},
    {0x0008, EVMC_BYZANTIUM, ecpairing_analyze, ecpairing_execute},
    {0x0009, EVMC_ISTANBUL, blake2bf_analyze, blake2bf_execute},
    {0x000a, EVMC_CANCUN, point_evaluation_analyze, point_evaluation_execute},
    {0x000b, EVMC_PRAGUE, bls12_g1add_analyze, bls12_g1add_execute},
    {0x000c, EVMC_PRAGUE, bls12_g1msm_analyze, bls12_g1msm_execute},
    {0x000d, EVMC_PRAGUE, bls12_g2add_analyze, bls12_g2add_execute},
    {0x000e, EVMC_PRAGUE, bls12_g2msm_analyze, bls12_g2msm_execute},
    {0x000f, EVMC_PRAGUE, bls12_pairing_check_analyze, bls12_pairing_check_execute},
    {0x0010, EVMC_PRAGUE, bls12_map_fp_to_g1_analyze, bls12_map_fp_to_g1_execute},
    {0x0011, EVMC_PRAGUE, bls12_map_fp2_to_g2_analyze, bls12_map_fp2_to_g2_execute},
    {0x0100, EVMC_OSAKA, p256verify_analyze, p256verify_execute},
}};

constexpr auto LOOKUP_TABLE_SIZE = [] {
    PrecompileLookupIndex max_idx = 0;
    for (const auto& trait : traits)
        max_idx = std::max(max_idx, trait.address);
    return max_idx + 1;
}();

PrecompileLookupIndex to_lookup_index(const evmc::address& addr) noexcept
{
    static constexpr auto ADDRESS_SIZE = sizeof(addr.bytes);
    return static_cast<PrecompileLookupIndex>(
        (addr.bytes[ADDRESS_SIZE - 2] << 8) | addr.bytes[ADDRESS_SIZE - 1]);
}
}  // namespace

bool is_precompile(evmc_revision rev, const evmc::address& addr) noexcept
{
    static constexpr auto AVAILABILITY_LOOKUP_TABLE = [] {
        using Entry = std::underlying_type_t<evmc_revision>;
        std::array<Entry, LOOKUP_TABLE_SIZE> table{};
        std::ranges::fill(table, std::numeric_limits<Entry>::max());
        for (const auto& trait : traits)
            table[trait.address] = stdx::to_underlying(trait.since);
        return table;
    }();

    if (addr >= evmc::address{AVAILABILITY_LOOKUP_TABLE.size()})
        return false;
    return AVAILABILITY_LOOKUP_TABLE[to_lookup_index(addr)] <= stdx::to_underlying(rev);
}

evmc::Result call_precompile(evmc_revision rev, const evmc_message& msg) noexcept
{
    static constexpr auto EXECUTION_LOOKUP_TABLE = [] {
        struct Entry
        {
            decltype(PrecompileTraits::analyze) analyze = nullptr;
            decltype(PrecompileTraits::execute) execute = nullptr;
        };
        std::array<Entry, LOOKUP_TABLE_SIZE> table{};
        for (const auto& trait : traits)
            table[trait.address] = {trait.analyze, trait.execute};
        return table;
    }();

    assert(msg.gas >= 0);

    const auto [analyze, execute] = EXECUTION_LOOKUP_TABLE[to_lookup_index(msg.code_address)];

    const bytes_view input{msg.input_data, msg.input_size};
    const auto [gas_cost, max_output_size] = analyze(input, rev);
    const auto gas_left = msg.gas - gas_cost;
    if (gas_left < 0)
        return evmc::Result{EVMC_OUT_OF_GAS};

    // Allocate buffer for the precompile's output and pass its ownership to evmc::Result.
    // TODO: This can be done more elegantly by providing constructor evmc::Result(std::unique_ptr).
    const auto output_data = new (std::nothrow) uint8_t[max_output_size];  // TODO: handle nullptr.
    const auto [status_code, output_size] =
        execute(msg.input_data, msg.input_size, output_data, max_output_size);
    const evmc_result result{status_code, status_code == EVMC_SUCCESS ? gas_left : 0, 0,
        output_data, output_size,
        [](const evmc_result* res) noexcept { delete[] res->output_data; }};
    return evmc::Result{result};
}
}  // namespace evmone::state
