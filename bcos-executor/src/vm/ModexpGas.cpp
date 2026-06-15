/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas: EIP-198 / EIP-2565 / EIP-7883 by revision.
 *  @file ModexpGas.cpp
 */

#include "ModexpGas.h"
#include "../Common.h"
#include "EvmPrecompiledAddress.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <limits>

using namespace std;
using namespace bcos;

namespace bcos::executor
{
namespace
{
bigint parseBigEndianRightPadded(bytesConstRef in, bigint const& begin, bigint const& count)
{
    if (begin > in.count())
    {
        return 0;
    }
    assert(count <= numeric_limits<size_t>::max() / 8);

    size_t const beginOffset{begin};
    size_t const countBytes{count};

    bytesConstRef const cropped =
        in.getCroppedData(beginOffset, min(countBytes, in.count() - beginOffset));

    bigint ret = fromBigEndian<bigint>(cropped);
    assert(countBytes - cropped.count() <= numeric_limits<size_t>::max() / 8);
    ret <<= 8 * (countBytes - cropped.count());
    return ret;
}

bigint parseHeaderLen(bytesConstRef in, size_t offset)
{
    return parseBigEndianRightPadded(in, offset, 32);
}

bigint expLengthAdjustEip2565(bigint const& expOffset, bigint const& expLength, bytesConstRef in)
{
    if (expLength <= 32)
    {
        bigint const exp(parseBigEndianRightPadded(in, expOffset, expLength));
        return exp ? msb(exp) : 0;
    }
    bigint const expFirstWord(parseBigEndianRightPadded(in, expOffset, 32));
    size_t const highestBit(expFirstWord ? msb(expFirstWord) : 0);
    return 8 * (expLength - 32) + highestBit;
}

bigint multComplexityEip198(bigint const& x)
{
    if (x <= 64)
    {
        return x * x;
    }
    if (x <= 1024)
    {
        return (x * x) / 4 + 96 * x - 3072;
    }
    return (x * x) / 16 + 480 * x - 199680;
}

bigint modexpMultComplexityEip2565(bigint const& maxLenBytes)
{
    bigint const words = (maxLenBytes + 7) / 8;
    return words * words;
}

bigint modexpMultComplexityEip7883(size_t baseLen, size_t modLen)
{
    size_t const maxLen = std::max(baseLen, modLen);
    if (maxLen <= 32)
    {
        return 16;
    }
    bigint const words = (bigint(maxLen) + 7) / 8;
    return 2 * words * words;
}

bigint modexpIterationCountEip7883(size_t expLen, bytesConstRef in, size_t baseLen)
{
    size_t const expOffset = 96 + baseLen;
    if (expLen <= 32)
    {
        bigint const exp = expLen == 0 ? 0 : parseBigEndianRightPadded(in, expOffset, expLen);
        if (expLen <= 32 && exp == 0)
        {
            return 0;
        }
        return exp ? msb(exp) : 0;
    }
    bigint const expHead(parseBigEndianRightPadded(in, expOffset, 32));
    static bigint const kMask256 = (bigint(1) << 256) - 1;
    bigint const exp256 = expHead & kMask256;
    size_t const highestBit = exp256 ? msb(exp256) : 0;
    return 16 * (expLen - 32) + highestBit;
}

bigint calcModexpGasEip198(bytesConstRef in)
{
    bigint const baseLength(parseHeaderLen(in, 0));
    bigint const expLength(parseHeaderLen(in, 32));
    bigint const modLength(parseHeaderLen(in, 64));

    bigint const maxLength(max(modLength, baseLength));
    bigint const adjustedExpLength(expLengthAdjustEip2565(baseLength + 96, expLength, in));

    return multComplexityEip198(maxLength) * max<bigint>(adjustedExpLength, 1) / 20;
}

bigint calcModexpGasEip2565(bytesConstRef in)
{
    bigint const baseLength(parseHeaderLen(in, 0));
    bigint const expLength(parseHeaderLen(in, 32));
    bigint const modLength(parseHeaderLen(in, 64));

    bigint const maxLength(max(modLength, baseLength));
    bigint const iterationCount(expLengthAdjustEip2565(baseLength + 96, expLength, in));
    bigint const complexity = modexpMultComplexityEip2565(maxLength);
    bigint const gas = complexity * max<bigint>(iterationCount, 1) / 3;
    static bigint const kMinGas{200};
    return gas < kMinGas ? kMinGas : gas;
}

bigint calcModexpGasEip7883(bytesConstRef in)
{
    auto const lens = parseModexpLengths(in);
    bigint const mc = modexpMultComplexityEip7883(lens.baseLen, lens.modLen);
    bigint const iter = modexpIterationCountEip7883(lens.expLen, in, lens.baseLen);
    bigint const gas = mc * max<bigint>(iter, 1);
    static bigint const kMinGas{500};
    return gas < kMinGas ? kMinGas : gas;
}
}  // namespace

ModexpLengths parseModexpLengths(bytesConstRef input)
{
    ModexpLengths out;
    static bigint const kUint64Max = (bigint(1) << 64) - 1;

    auto const base = parseHeaderLen(input, 0);
    auto const exp = parseHeaderLen(input, 32);
    auto const mod = parseHeaderLen(input, 64);

    auto assign = [&](bigint const& v, size_t& dst) {
        if (v > kUint64Max)
        {
            out.overflow = true;
            dst = 0;
            return;
        }
        dst = static_cast<size_t>(v.convert_to<uint64_t>());
    };

    assign(base, out.baseLen);
    assign(exp, out.expLen);
    assign(mod, out.modLen);
    return out;
}

bool validateModexpEip7823(bytesConstRef input, evmc_revision revision)
{
    if (revision < EVMC_OSAKA)
    {
        return true;
    }
    auto const lens = parseModexpLengths(input);
    if (lens.overflow)
    {
        return false;
    }
    if (lens.baseLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    if (lens.expLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    if (lens.modLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    return true;
}

bool shouldRejectModexpEip7823(evmc_address const& addr, bytesConstRef input,
    ledger::Features const& features, evmc_revision revision) noexcept
{
    if (!isModexpPrecompileEvmcAddress(addr))
    {
        return false;
    }
    if (!modexpEip7823Enabled(features, revision))
    {
        return false;
    }
    return !validateModexpEip7823(input, revision);
}

bool shouldRejectModexpEip7823(std::string_view addr, bytesConstRef input,
    ledger::Features const& features, evmc_revision revision) noexcept
{
    if (!isModexpPrecompileAddress(addr))
    {
        return false;
    }
    if (!modexpEip7823Enabled(features, revision))
    {
        return false;
    }
    return !validateModexpEip7823(input, revision);
}

bigint calcModexpGas(bytesConstRef input, evmc_revision revision)
{
    if (revision >= EVMC_OSAKA)
    {
        return calcModexpGasEip7883(input);
    }
    if (revision >= EVMC_BERLIN)
    {
        return calcModexpGasEip2565(input);
    }
    return calcModexpGasEip198(input);
}

bigint calcModexpGasEip198Public(bytesConstRef input)
{
    return calcModexpGasEip198(input);
}

}  // namespace bcos::executor
