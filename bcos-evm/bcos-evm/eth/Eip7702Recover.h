#pragma once

// Shared EIP-7702 authorization handling, used by BOTH the opstack transition
// (OpTransition.cpp) and the eth path (eth/state/state.cpp transition) so the two execution
// paths verify and apply authorizations identically — real ecrecover, not the upstream
// signer-shortcut stub, and ONE copy of the list-processing rules.
//
// Both callers previously carried their own verbatim copy of process_authorization_list plus
// the two EIP-7702 constants. The bodies were identical down to the control flow (they differed
// only in namespace qualification and comment wording), so the duplication bought nothing and
// risked the two consensus paths silently diverging on authorization validity. The eth-path copy
// additionally had no test coverage of its own; sharing this implementation puts it under the
// opstack 7702 suite.

#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmone/delegation.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <optional>
#include <span>
#include <test/utils/rlp.hpp>
#include <utility>

namespace bcos::evm::eth
{
/// Secp256k1's N/2 is the upper bound of an EIP-2 canonical signature's s value.
inline constexpr auto SECP256K1N_OVER_2 = evmmax::secp256k1::Curve::ORDER / 2;

/// EIP-7702 authorization magic byte (prefix of the signing hash).
inline constexpr uint8_t kSetCodeMagic = 0x05;

/// Recover the authority address from an EIP-7702 authorization via ecrecover.
/// signing hash = keccak256(0x05 || rlp([chain_id, address, nonce]))
inline std::optional<evmc::address> recoverAuthority(const evmone::state::Authorization& auth)
{
    auto msg = evmone::bytes{kSetCodeMagic} +
               evmone::rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = evmone::keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}

/// EIP-7702: The cost of authorization that sets delegation to an account that didn't exist before.
inline constexpr int64_t AUTHORIZATION_EMPTY_ACCOUNT_COST = 25000;
/// EIP-7702: The cost of authorization that sets delegation to an account that already exists.
inline constexpr int64_t AUTHORIZATION_BASE_COST = 12500;

/// Process a transaction's EIP-7702 authorization list against @p state, returning the gas
/// refund earned by authorities that already existed. The SINGLE implementation both execution
/// paths call — see the file header for why this is not duplicated per path.
///
/// Ported from evmone's transition() with the signer-shortcut stub replaced by real ecrecover:
/// a pre-set auth.signer (test shortcut) is honored, otherwise the authority is recovered from
/// the signature, and a recovery failure skips that authorization.
inline int64_t processAuthorizationList(evmone::state::State& state, uint64_t chainId,
    const evmone::state::AuthorizationList& authorizationList)
{
    int64_t delegationRefund = 0;
    for (const auto& auth : authorizationList)
    {
        // 1. Verify the chain id is either 0 or the chain's current ID.
        if (auth.chain_id != 0 && auth.chain_id != chainId)
            continue;

        // 2. Verify the nonce is less than 2**64 - 1.
        if (auth.nonce == evmone::state::Account::NonceMax)
            continue;

        // 3. Verify if the signer has been successfully recovered from the signature.
        //    authority = ecrecover(...)
        // y_parity must be 0 or 1 for EIP-7702/2930 signatures.
        if (auth.v > 1)
            continue;
        // s value must be less than or equal to secp256k1n/2, as specified in EIP-2.
        // Validated before ecrecover, as op-geth does (ValidateSignatureValues before Recover).
        if (auth.s > SECP256K1N_OVER_2)
            continue;

        // Recover signer: use pre-set signer if available (test shortcut); otherwise ecrecover.
        std::optional<evmc::address> signer = auth.signer;
        if (!signer.has_value())
            signer = recoverAuthority(auth);
        if (!signer.has_value())
            continue;  // ecrecover failed → skip this authorization

        // Get or create the authority account.
        // It is still empty at this point until nonce bump following successful authorization.
        auto& authority = state.get_or_insert(*signer, {.erase_if_empty = true});

        // 4. Add authority to accessed_addresses (as defined in EIP-2929.)
        authority.access_status = EVMC_ACCESS_WARM;

        // 5. Verify the code of authority is either empty or already delegated.
        if (authority.code_hash != evmone::state::Account::EMPTY_CODE_HASH &&
            !evmone::is_code_delegated(state.get_code(*signer)))
            continue;

        // 6. Verify the nonce of authority is equal to nonce.
        // In case authority does not exist in the trie, verify that nonce is equal to 0.
        if (auth.nonce != authority.nonce)
            continue;

        // 7. Add PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST gas to the global refund counter
        // if authority exists in the trie.
        // Successful authorization validation makes an account non-empty.
        // We apply the refund only if the account has existed before.
        // We detect "exists in the trie" by inspecting _empty_ property (EIP-161) because _empty_
        // implies an account doesn't exist in the state (EIP-7523).
        if (!authority.is_empty())
        {
            static constexpr auto EXISTING_AUTHORITY_REFUND =
                AUTHORIZATION_EMPTY_ACCOUNT_COST - AUTHORIZATION_BASE_COST;
            delegationRefund += EXISTING_AUTHORITY_REFUND;
        }

        // As a special case, if address is 0 do not write the designation.
        // Clear the account's code and reset the account's code hash to the empty hash.
        if (evmc::is_zero(auth.addr))
        {
            if (authority.code_hash != evmone::state::Account::EMPTY_CODE_HASH)
            {
                authority.code_changed = true;
                authority.code.clear();
                authority.code_hash = evmone::state::Account::EMPTY_CODE_HASH;
            }
        }
        // 8. Set the code of authority to be 0xef0100 || address. This is a delegation designation.
        else
        {
            auto new_code =
                evmone::state::bytes(evmone::DELEGATION_MAGIC) + evmone::state::bytes(auth.addr);
            if (authority.code != new_code)
            {
                // We are doing this only if the code is different to make the state diff precise.
                authority.code_changed = true;
                authority.code = std::move(new_code);
                authority.code_hash = evmone::keccak256(authority.code);
            }
        }

        // 9. Increase the nonce of authority by one.
        ++authority.nonce;
    }
    return delegationRefund;
}
}  // namespace bcos::evm::eth
