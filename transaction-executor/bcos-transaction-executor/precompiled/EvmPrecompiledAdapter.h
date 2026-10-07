/// @file EvmPrecompiledAdapter.h
/// @brief Adapts the shared EVM precompile execute functions
///        (ethereum-executor/EVMPrecompiles.h, namespace bcos::executor_v1::eth::evm)
///        to the bcos-evm PrecompiledExecutor shape ({success, output}), so the v1
///        executor and the ethereum-executor share ONE canonical implementation of
///        each precompile's execution.
///
/// Consensus semantics stay on the BCOS side and are NOT unified:
///  - Pricing keeps using the PrecompiledRegistrar pricers / fixed constants in
///    PrecompiledManager.cpp (BCOS repricing is governed by feature flags, not evmc
///    revisions — e.g. modexp is still EIP-198-priced here, while the eth::evm
///    analyze functions price EIP-2565/7883).
///  - Failure keeps the legacy bcos-evm shape: callBuiltinPrecompiled maps
///    {success=false} to EVMC_REVERT with the remaining gas left intact, and the
///    revert data is the legacy failure output (64/32 zero bytes for the bn254
///    contracts, empty for BLS/blake2) — reproduced here via failureOutputSize.
///
/// Two precompiles are deliberately NOT routed through this adapter:
///  - ecrecover: bcos::crypto::ecRecover (wedpr keccak + secp256k1Recover) accepts
///    recovery ids 0-3 taken from the low byte of v and right-pads short input,
///    while eth::evm::ecrecover_execute requires the full 256-bit v ∈ {27,28};
///    the edge behaviour differs, so the legacy executor stays.
///  - modexp: the legacy executor parses the length header as full 256-bit words
///    (values > SIZE_MAX collapse to 0), while expmod_parse_input in the shared
///    implementation reads the low 32 bits; declared lengths >= 2^32 would
///    diverge, so the legacy executor stays (it already calls the same
///    evmone::crypto::modexp primitive).

#pragma once

#include "bcos-executor/src/vm/Precompiled.h"
#include <ethereum-executor/EVMPrecompiles.h>
#include <functional>
#include <utility>

namespace bcos::executor_v1
{
namespace eth_evm = bcos::executor_v1::eth::evm;

/// Signature of the shared execute functions (eth::evm::*_execute).
using EvmPrecompileExecute = eth_evm::ExecutionResult (*)(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;

/// Adapts an eth::evm execute function to the bcos-evm PrecompiledExecutor
/// convention. On success the output is the first output_size bytes of a
/// maxOutputSize buffer; on failure the result is {false, zeroes(failureOutputSize)}
/// — byte-identical to the legacy bcos-evm executors.
///
/// @p precheck (optional) replicates the legacy early input validation. It also
/// keeps inputs the shared execute functions only assert on (MSM / pairing input
/// divisibility, blake2 input length) from reaching them — those asserts compile
/// out under NDEBUG.
inline executor::PrecompiledExecutor adaptEvmPrecompiled(EvmPrecompileExecute execute,
    size_t maxOutputSize, size_t failureOutputSize,
    std::function<bool(bytesConstRef)> precheck = {})
{
    return [=](bytesConstRef in) -> std::pair<bool, bytes> {
        if (precheck && !precheck(in))
        {
            return {false, bytes(failureOutputSize, 0)};
        }
        bytes output(maxOutputSize, 0);
        const auto [status, outputSize] =
            execute(in.data(), in.size(), output.data(), output.size());
        if (status != EVMC_SUCCESS)
        {
            return {false, bytes(failureOutputSize, 0)};
        }
        output.resize(outputSize);
        return {true, std::move(output)};
    };
}

/// EIP-152 blake2bf: the legacy executor rejects any input that is not exactly
/// 213 bytes (the shared execute only asserts it).
inline bool blake2InputOk(bytesConstRef in)
{
    return in.size() == 213;
}

/// EIP-2537 MSM / pairing inputs must be non-empty and a whole number of pairs;
/// the shared execute functions assume _analyze already enforced this.
inline auto multipleOf(size_t pairSize)
{
    return [pairSize](bytesConstRef in) { return !in.empty() && in.size() % pairSize == 0; };
}

/// identity (0x04): output is the input itself, so the buffer is sized dynamically.
inline std::pair<bool, bytes> identityExecutor(bytesConstRef in)
{
    bytes output(in.size(), 0);
    eth_evm::identity_execute(in.data(), in.size(), output.data(), output.size());
    return {true, std::move(output)};
}

/// p256verify (0x0100): never fails — wrong-size input or an invalid signature
/// yields {true, {}} per EIP-7212/RIP-7212 (matched by the shared execute).
inline std::pair<bool, bytes> p256VerifyExecutor(bytesConstRef in)
{
    bytes output(32, 0);
    const auto [status, outputSize] =
        eth_evm::p256verify_execute(in.data(), in.size(), output.data(), output.size());
    output.resize(status == EVMC_SUCCESS ? outputSize : 0);
    return {true, std::move(output)};
}
}  // namespace bcos::executor_v1
