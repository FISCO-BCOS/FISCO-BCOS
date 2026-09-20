#pragma once

#include <array>
#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>

namespace bcos::evm::opstack
{
struct OpFeeParams;
struct OpForkConfig;

namespace detail
{
constexpr int64_t c_l1CostIntercept = -42585600;
constexpr int64_t c_l1CostFastlzCoef = 836500;
constexpr int64_t c_minTxSizeScaled = 100000000;
// The 1e6 scaling factor for estimatedDaSizeScaled (semantically unrelated to the operator
// scalar's 1e6; do not merge them).
constexpr int64_t c_daSizeScaleDivisor = 1'000'000;

// Operator-fee formula primitives (see computeOperatorCost below). Exposed here rather than
// file-local so the op-revm oracle parity test asserts against the SAME constants the
// production formula consumes, not against a copy of their literals.
//   * Isthmus+: gas * scalar / c_operatorFeeScalarDivisor + constant
inline constexpr int64_t c_operatorFeeScalarDivisor = 1000000;
//   * Jovian:  gas * scalar * c_jovianOperatorFeeMultiplier + constant
inline constexpr int64_t c_jovianOperatorFeeMultiplier = 100;

// Port of op-geth FlzCompressLen: length of output if serializedTx were FastLZ-compressed.
// Inline so header-only callers (the engine's Jovian DA-footprint equality gate reaches it via
// bcos::evm::opstack::daFootprintOfEnvelopes) do not add a bcos-evm-opstack link dependency to
// the exported engine archive.
inline uint32_t flzCompressLenImpl(evmc::bytes_view ib) noexcept
{
    uint32_t n = 0;
    // The hash table — 8192 entries of 16 bytes, i.e. 128 KiB per thread (op-geth's original
    // is 8192 4-byte positions = 32 KiB; the generation tag below is what widened the entry) —
    // is reused across calls via a per-entry generation tag instead
    // of being stack-allocated and zero-filled on every run: the engine's DA-footprint gate
    // and the per-tx Jovian pricing each compress every envelope, and the table clear
    // dominated small envelopes. An entry tagged with the current generation holds exactly
    // what the zeroed table held; an entry from an older generation reads as empty
    // (position 0) — the same candidates a fresh table produced, so the compressed length
    // is unchanged. The 64-bit generation cannot wrap onto a live entry.
    struct HashEntry
    {
        uint32_t position;
        std::uint64_t generation;
    };
    thread_local std::array<HashEntry, 8192> ht{};
    thread_local std::uint64_t htGeneration = 0;
    const auto thisGeneration = ++htGeneration;

    auto const* const bytes = ib.data();
    auto const len = static_cast<uint32_t>(ib.size());

    auto u24 = [&](uint32_t i) -> uint32_t {
        return static_cast<uint32_t>(bytes[i]) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
               (static_cast<uint32_t>(bytes[i + 2]) << 16);
    };
    auto cmp = [&](uint32_t p, uint32_t q, uint32_t e) -> uint32_t {
        uint32_t l = 0;
        for (e -= q; l < e; ++l)
        {
            if (bytes[p + l] != bytes[q + l])
            {
                e = 0;
            }
        }
        return l;
    };
    auto literals = [&](uint32_t r) {
        n += 0x21 * (r / 0x20);
        r %= 0x20;
        if (r != 0)
        {
            n += r + 1;
        }
    };
    auto match = [&](uint32_t l) {
        --l;
        n += 3 * (l / 262);
        if (l % 262 >= 6)
        {
            n += 3;
        }
        else
        {
            n += 2;
        }
    };
    auto hash = [](uint32_t v) -> uint32_t { return ((2654435769U * v) >> 19) & 0x1fff; };
    auto setNextHash = [&](uint32_t ip) -> uint32_t {
        ht[hash(u24(ip))] = HashEntry{ip, thisGeneration};
        return ip + 1;
    };

    uint32_t a = 0;
    uint32_t ipLimit = len - 13;
    if (len < 13)
    {
        ipLimit = 0;
    }

    for (uint32_t ip = a + 2; ip < ipLimit;)
    {
        uint32_t r = 0;  // read after the loop by cmp(); must outlive it
        for (;;)
        {
            auto const s = u24(ip);
            auto const h = hash(s);
            r = ht[h].generation == thisGeneration ? ht[h].position : 0;
            ht[h] = HashEntry{ip, thisGeneration};
            const uint32_t d = ip - r;
            if (ip >= ipLimit)
            {
                break;
            }
            ++ip;
            if (d <= 0x1fff && s == u24(r))
            {
                break;
            }
        }
        if (ip >= ipLimit)
        {
            break;
        }
        --ip;
        if (ip > a)
        {
            literals(ip - a);
        }
        auto const l = cmp(r + 3, ip + 3, ipLimit + 9);
        match(l);
        ip = setNextHash(setNextHash(ip + l));
        a = ip;
    }
    literals(len - a);
    return n;
}
}  // namespace detail

/// FastLZ-compressed length (the Fjord DA regression input). Port of op-geth FlzCompressLen;
/// byte-for-byte aligned with production.
inline uint32_t flzCompressLen(evmc::bytes_view data) noexcept
{
    return detail::flzCompressLenImpl(data);
}

/// estimatedSize (x1e6) = max(100e6, -42585600 + 836500*fastlzSize).
inline intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept
{
    const int64_t scaled =
        detail::c_l1CostIntercept + detail::c_l1CostFastlzCoef * static_cast<int64_t>(fastlzSize);
    const int64_t clamped = scaled < detail::c_minTxSizeScaled ? detail::c_minTxSizeScaled : scaled;
    return intx::uint256{static_cast<uint64_t>(clamped)};
}

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; takes an already-computed flz so the
/// caller need not re-compress.
///
/// DELIBERATE DIVERGENCE FROM op-geth, and the one thing to know before reusing this: flzLen==0
/// returns 0, whereas op-geth has no such branch and its clamp would yield 100
/// (estimatedDASizeScaled floors at MinTransactionSizeScaled = 100e6). flz is 0 only for empty
/// input, which is not a transaction — op-geth never evaluates the formula there, so this is a
/// choice about undefined territory rather than a mismatch on any real input, and charging a
/// 100-byte minimum for no data would be the stranger answer.
///
/// It matters because these two entry points take a raw flz rather than bytes. A block-level DA
/// accumulator that passes a cached or defaulted 0 gets 0 here, silently, instead of the
/// clamped minimum. If that is ever a real caller, validate flz at the call site — do not
/// "fix" this to match the clamp, which would start charging for transactions that carry no
/// data. EstimatedDaSizeDividesScaledBy1e6 pins both halves.
inline uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept
{
    if (flzLen == 0)
        return 0;
    return static_cast<uint64_t>(
        estimatedDaSizeScaled(flzLen) / intx::uint256{detail::c_daSizeScaleDivisor});
}

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; returns 0 for an empty envelope (same
/// deliberate divergence as estimatedDaSizeFromFlz above).
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;

/// Ecotone L1 calldata gas: zeroes*4 + nonZeroes*16 (no pre-Regolith +68).
uint64_t bedrockCalldataGasUsed(evmc::bytes_view signedTxEnvelope) noexcept;

/// L1 data fee, Fjord+ FastLZ branch; takes an already-computed flz so the caller need not
/// re-compress. flzLen==0 returns 0 — same deliberate divergence from op-geth's clamp as
/// estimatedDaSizeFromFlz above, and the same caveat for callers holding a cached flz.
intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept;

/// L1 data fee, selected by cfg.l1_fee_model: Bedrock = (calldataGas + overhead) * l1BaseFee *
/// scalar / 1e6; Ecotone = calldataGas formula, but only when the Ecotone input slots are live
/// (ecotoneL1SlotsLive) — zero slots keep the Bedrock formula (the Ecotone activation block still
/// runs setL1BlockValues); Fjord = FastLZ. Returns 0 for an empty envelope; the caller guarantees
/// deposits are always zero.
intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept;

/// Operator fee: Isthmus gas*scalar/1e6+constant; Jovian gas*scalar*100+constant.
/// The bool overload takes the formula selection directly so a caller can pin it to a snapshot
/// (OpTxProperties) and stay consistent across the validate/transition split; the cfg overload
/// forwards cfg.has_jovian_operator_formula.
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, bool jovianFormula) noexcept;
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evm::opstack
