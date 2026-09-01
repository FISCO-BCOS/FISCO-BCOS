/// @file EVMSupport.h
/// @brief Self-contained EVM support utilities for the pure-Ethereum executor:
///        keccak256, validation error codes, blob parameters/price, CREATE /
///        CREATE2 address derivation, and EIP-7702 authority recovery.
///
/// These were previously pulled in from bcos-evm (eth/state/{hash_utils,errors,
/// block,host}.{hpp,cpp} and eth/Eip7702Recover.h). They are ported here,
/// renamed into the `bcos::executor_v1::eth::evm` namespace so the ethereum-
/// executor no longer depends on the bcos-evm library and never ODR-collides
/// with it when both are linked into the same binary.
///
/// Only external dependencies remain: the evmone package
/// (<evmone_precompiles/keccak.hpp> keccak256, <evmone_precompiles/secp256k1.hpp>
/// ecrecover / curve order) and bcos-codec's canonical RLP encoder
/// (<bcos-codec/rlp/RLPEncode.h>) for the CREATE/CREATE2/EIP-7702 hashes.

#pragma once

#include "bcos-framework/protocol/Authorization.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmone_precompiles/keccak.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <intx/intx.hpp>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace bcos::executor_v1::eth::evm
{

/// Store a bcos::u256 as a 32-byte big-endian evmc value (evmc::bytes32 or evmc::uint256be).
template <class EvmcBytes = evmc::bytes32>
inline EvmcBytes toEvmcBE(bcos::u256 const& val)
{
    EvmcBytes out{};
    static_assert(sizeof(out.bytes) == 32, "toEvmcBE targets 32-byte evmc values only");
    std::span<uint8_t, sizeof(out.bytes)> view{out.bytes};
    bcos::toBigEndian(val, view);
    return out;
}

/// Load a 32-byte big-endian evmc value as a bcos::u256.
inline bcos::u256 fromEvmcBE(auto const& be)
    requires(sizeof(be.bytes) == 32)
{
    return bcos::fromBigEndian<bcos::u256>(be.bytes);
}

/// A transaction log entry (ported evmone state::Log).
struct Log
{
    evmc::address addr;
    evmc::bytes data;
    std::vector<evmc::bytes32> topics;
};

/// Computes Keccak-256 hash out of input bytes (wrapper of ethash::keccak256).
inline evmc::bytes32 keccak256(evmc::bytes_view data) noexcept
{
    return std::bit_cast<evmc::bytes32>(ethash::keccak256(data.data(), data.size()));
}

// ---- Transaction validation error codes (ported evmone errors.hpp) ----

enum ErrorCode : int  // NOLINT(*-use-enum-class)
{
    SUCCESS = 0,
    INTRINSIC_GAS_TOO_LOW,
    TX_TYPE_NOT_SUPPORTED,
    INSUFFICIENT_FUNDS,
    NONCE_HAS_MAX_VALUE,
    NONCE_TOO_HIGH,
    NONCE_TOO_LOW,
    TIP_GT_FEE_CAP,
    FEE_CAP_LESS_THAN_BLOCKS,
    BLOB_FEE_CAP_LESS_THAN_BLOCKS,
    GAS_LIMIT_REACHED,
    SENDER_NOT_EOA,
    INIT_CODE_SIZE_LIMIT_EXCEEDED,
    CREATE_BLOB_TX,
    EMPTY_BLOB_HASHES_LIST,
    INVALID_BLOB_HASH_VERSION,
    BLOB_GAS_LIMIT_EXCEEDED,
    CREATE_SET_CODE_TX,
    EMPTY_AUTHORIZATION_LIST,
    MAX_GAS_LIMIT_EXCEEDED,
    UNKNOWN_ERROR,
};

/// Obtains a reference to the static error category object for EVM errors.
inline const std::error_category& evm_error_category() noexcept
{
    struct Category : std::error_category
    {
        [[nodiscard]] const char* name() const noexcept final { return "evm"; }

        [[nodiscard]] std::string message(int ev) const noexcept final
        {
            switch (ev)
            {
            case SUCCESS:
                return "";
            case INTRINSIC_GAS_TOO_LOW:
                return "intrinsic gas too low";
            case TX_TYPE_NOT_SUPPORTED:
                return "transaction type not supported";
            case INSUFFICIENT_FUNDS:
                return "insufficient funds for gas * price + value";
            case NONCE_HAS_MAX_VALUE:
                return "nonce has max value:";
            case NONCE_TOO_HIGH:
                return "nonce too high";
            case NONCE_TOO_LOW:
                return "nonce too low";
            case TIP_GT_FEE_CAP:
                return "max priority fee per gas higher than max fee per gas";
            case FEE_CAP_LESS_THAN_BLOCKS:
                return "max fee per gas less than block base fee";
            case BLOB_FEE_CAP_LESS_THAN_BLOCKS:
                return "max blob fee per gas less than block base fee";
            case GAS_LIMIT_REACHED:
                return "gas limit reached";
            case SENDER_NOT_EOA:
                return "sender not an eoa:";
            case INIT_CODE_SIZE_LIMIT_EXCEEDED:
                return "max initcode size exceeded";
            case CREATE_BLOB_TX:
                return "blob transaction must not be a create transaction";
            case EMPTY_BLOB_HASHES_LIST:
                return "empty blob hashes list";
            case INVALID_BLOB_HASH_VERSION:
                return "invalid blob hash version";
            case BLOB_GAS_LIMIT_EXCEEDED:
                return "blob gas limit exceeded";
            case CREATE_SET_CODE_TX:
                return "set code transaction must not be a create transaction";
            case EMPTY_AUTHORIZATION_LIST:
                return "empty authorization list";
            case MAX_GAS_LIMIT_EXCEEDED:
                return "max gas limit exceeded";
            case UNKNOWN_ERROR:
                return "Unknown error";
            default:
                assert(false);
                return "Wrong error code";
            }
        }
    };

    static const Category category_instance;
    return category_instance;
}

/// Creates error_code object out of an EVM error code value.
inline std::error_code make_error_code(ErrorCode errc) noexcept
{
    return {errc, evm_error_category()};
}

// ---- Blob parameters (ported evmone blob_params.hpp / transaction.hpp) ----

/// The cost of a single blob in gas units (EIP-4844).
constexpr auto GAS_PER_BLOB = 0x20000;  // 2**17

/// The maximum number of blobs that can be included in a transaction (EIP-7594).
constexpr auto MAX_TX_BLOB_COUNT = 6;

/// The maximum allowed gas limit for a transaction (EIP-7825).
/// Defined in bcos-framework/protocol/TxGasModel.h -- admission enforces the same cap.
using bcos::protocol::MAX_TX_GAS_LIMIT;

/// The blob schedule entry for an EVM revision (EIP-7840).
struct BlobParams
{
    uint16_t target = 0;
    uint16_t max = 0;
    uint32_t base_fee_update_fraction = 0;
};

/// Max amount of blob gas allowed in block (ported evmone block.cpp).
inline uint64_t max_blob_gas_per_block(const BlobParams& blob_params) noexcept
{
    // Compute in uint64_t: GAS_PER_BLOB (int) * uint16_t max would otherwise
    // be evaluated in int (signed overflow UB once max >= 16384).
    return static_cast<uint64_t>(blob_params.max) * GAS_PER_BLOB;
}

/// Computes the current blob gas price based on the excess blob gas
/// (ported evmone block.cpp, EIP-4844 helpers).
inline bcos::u256 compute_blob_gas_price(
    const BlobParams& blob_params, uint64_t excess_blob_gas) noexcept
{
    /// A helper function approximating `factor * e ** (numerator / denominator)`.
    static constexpr auto fake_exponential = [](uint64_t factor, uint64_t numerator,
                                                 uint64_t denominator) noexcept {
        bcos::u256 i = 1;
        bcos::u256 output = 0;
        bcos::u256 numerator_accum = factor * denominator;
        const bcos::u256 numerator256 = numerator;
        while (numerator_accum > 0)
        {
            output += numerator_accum;
            // Ensure the multiplication won't overflow 256 bits: widen to u512, as
            // the original's intx::umul did.
            if (const auto p = bcos::u512(numerator_accum) * bcos::u512(numerator256);
                p <= bcos::u512(std::numeric_limits<bcos::u256>::max()))
                numerator_accum = bcos::u256(p) / (denominator * i);
            else
                return std::numeric_limits<bcos::u256>::max();
            i += 1;
        }
        return output / denominator;
    };

    static constexpr auto MIN_BLOB_GASPRICE = 1;
    const auto fraction = blob_params.base_fee_update_fraction;
    if (fraction == 0)
        return std::numeric_limits<bcos::u256>::max();  // degenerate schedule
    return fake_exponential(MIN_BLOB_GASPRICE, excess_blob_gas, fraction);
}

/// Computes the address of to-be-created contract with the CREATE scheme.
/// (ported evmone host.cpp; Yellow Paper, 7. Contract Creation).
/// RLP encoding is delegated to bcos-codec's canonical encoder:
/// addr = keccak256(rlp([sender, nonce]))[12..].
[[nodiscard]] inline evmc::address compute_create_address(
    const evmc::address& sender, uint64_t sender_nonce) noexcept
{
    bcos::bytes encoded;
    bcos::codec::rlp::encode(
        encoded, bcos::bytesConstRef{sender.bytes, sizeof(sender)}, sender_nonce);

    const auto base_hash = keccak256(evmc::bytes_view{encoded.data(), encoded.size()});
    evmc::address addr;
    std::copy_n(&base_hash.bytes[sizeof(base_hash) - sizeof(addr)], sizeof(addr), addr.bytes);
    return addr;
}

/// Computes the address of to-be-created contract with the CREATE2 scheme.
/// (ported evmone host.cpp).
[[nodiscard]] inline evmc::address compute_create2_address(
    const evmc::address& sender, const evmc::bytes32& salt, evmc::bytes_view init_code) noexcept
{
    const auto init_code_hash = keccak256(init_code);
    uint8_t buffer[1 + sizeof(sender) + sizeof(salt) + sizeof(init_code_hash)];
    static_assert(std::size(buffer) == 85);
    auto it = std::begin(buffer);
    *it++ = 0xff;
    it = std::copy_n(sender.bytes, sizeof(sender), it);
    it = std::copy_n(salt.bytes, sizeof(salt), it);
    std::copy_n(init_code_hash.bytes, sizeof(init_code_hash), it);
    const auto base_hash = keccak256({buffer, std::size(buffer)});
    evmc::address addr;
    std::copy_n(&base_hash.bytes[sizeof(base_hash) - sizeof(addr)], sizeof(addr), addr.bytes);
    return addr;
}

// ---- EIP-7702 authorization handling (ported bcos-evm Eip7702Recover.h) ----

/// Secp256k1's N/2 is the upper bound of an EIP-2 canonical signature's s value.
/// Converted once from evmone's intx constant — the curve API is the only intx
/// boundary left in this header. Runtime-initialized: do not read it from
/// another static initializer.
inline const bcos::u256 SECP256K1N_OVER_2 =
    fromEvmcBE(intx::be::store<evmc::bytes32>(evmmax::secp256k1::Curve::ORDER / 2));

/// EIP-7702 authorization magic byte (prefix of the signing hash).
inline constexpr uint8_t kSetCodeMagic = 0x05;

/// EIP-7702: The cost of authorization that sets delegation to an account that didn't exist
/// before. Defined in bcos-framework/protocol/TxGasModel.h -- it is part of the intrinsic-gas
/// formula, which admission must compute identically.
using bcos::protocol::AUTHORIZATION_EMPTY_ACCOUNT_COST;
/// EIP-7702: The cost of authorization that sets delegation to an account that already exists.
inline constexpr int64_t AUTHORIZATION_BASE_COST = 12500;

/// Recover the EIP-7702 authority signer from a bcos Authorization via real
/// ecrecover. signing hash = keccak256(0x05 || rlp([chain_id, address, nonce])),
/// where the RLP tuple is encoded with bcos-codec's canonical encoder.
///
/// NOTE: this performs recovery ONLY. EIP-2 canonical-s (s <= SECP256K1N_OVER_2),
/// y-parity <= 1, chain-id match and nonce != max are NOT checked here; the
/// caller must apply them before calling, as processAuthorizationList does in
/// bcos-evm/bcos-evm/eth/Eip7702Recover.h:68-83 (and its port in
/// EthereumTransition.h, split 4/4). Skipping them diverges from geth/op-geth.
inline std::optional<evmc::address> recoverAuthority(protocol::Authorization const& auth)
{
    // rlp([chain_id, address, nonce]).
    bcos::bytes tuple;
    bcos::codec::rlp::encode(tuple, auth.chainId, auth.address, auth.nonce);

    // signing hash = keccak256(0x05 || rlp([chain_id, address, nonce])).
    bcos::bytes msg;
    msg.reserve(1 + tuple.size());
    msg.push_back(kSetCodeMagic);
    msg.insert(msg.end(), tuple.begin(), tuple.end());

    const auto h = keccak256(evmc::bytes_view{msg.data(), msg.size()});
    const auto r = toEvmcBE(auth.r);
    const auto s = toEvmcBE(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}

}  // namespace bcos::executor_v1::eth::evm
