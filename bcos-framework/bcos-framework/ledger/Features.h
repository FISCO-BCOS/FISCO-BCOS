#pragma once
#include "../protocol/Protocol.h"
#include "../protocol/ProtocolTypeDef.h"
#include "../storage2/Storage.h"
#include "bcos-task/Task.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <array>
#include <bitset>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <ostream>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <set>
#include <string_view>

// Forward declarations only — the storage-I/O member templates below take these
// types as concept arguments / parameter types, which need name visibility but
// not full definitions at declaration time. Full definitions are pulled in by
// FeaturesStorage.h, where the implementations and concrete call sites live.
// Breaking the include of storage/Entry.h / StateKey.h here is what lets
// storage/Entry.h include Features.h without forming a cycle through
// StateKey.h's `using StateValue = storage::Entry`.
namespace bcos::storage
{
class Entry;
}
namespace bcos::executor_v1
{
class StateKey;
class StateKeyView;
}  // namespace bcos::executor_v1

namespace bcos::ledger
{
DERIVE_BCOS_EXCEPTION(NoSuchFeatureError);
class Features
{
public:
    // Use for storage key, do not change the enum name!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // feature_flags (Features::toFlagsNumber) packs each enabled flag at
    // bit = its EXPLICIT enum value below. Those values are persisted on-chain,
    // so each value is PERMANENT: never change or reuse a value, never delete a
    // flag's number. New flags take the next unused number — the declaration
    // position is free (you may group them anywhere), only the value matters.
    // magic_enum reflects values in its default range [-128, 127]. The
    // enum_contains assert catches any flag outside that range at compile time.
    // The static_assert keeps the max value ≤ 127 (bitset encoding constraint).
    enum class Flag
    {
        bugfix_revert = 0,  // https://github.com/FISCO-BCOS/FISCO-BCOS/issues/3629
        bugfix_statestorage_hash = 1,
        bugfix_evm_create2_delegatecall_staticcall_codecopy = 2,
        bugfix_event_log_order = 3,
        bugfix_call_noaddr_return = 4,
        bugfix_precompiled_codehash = 5,
        bugfix_dmc_revert = 6,
        bugfix_keypage_system_entry_hash = 7,
        bugfix_internal_create_redundant_storage = 8,  // perf internal create code and abi storage
        bugfix_internal_create_permission_denied = 9,
        bugfix_sharding_call_in_child_executive = 10,
        bugfix_empty_abi_reset = 11,  // support empty abi reset of same code
        bugfix_eip55_addr = 12,
        bugfix_eoa_as_contract = 13,
        bugfix_eoa_match_failed = 14,
        bugfix_evm_exception_gas_used = 15,
        bugfix_dmc_deploy_gas_used = 16,
        bugfix_staticcall_noaddr_return = 17,
        bugfix_support_transfer_receive_fallback = 18,
        bugfix_set_row_with_dirty_flag = 19,
        bugfix_rpbft_vrf_blocknumber_input = 20,
        bugfix_delete_account_code = 21,
        bugfix_policy1_empty_code_address = 22,
        bugfix_precompiled_gasused = 23,
        bugfix_nonce_not_increase_when_revert = 24,
        bugfix_set_contract_nonce_when_create = 25,
        bugfix_precompiled_gascalc = 26,
        bugfix_method_auth_sender = 27,
        bugfix_precompiled_evm_status = 28,
        bugfix_delegatecall_transfer = 29,
        bugfix_nonce_initialize = 30,
        bugfix_v1_timestamp = 31,
        bugfix_revert_logs = 32,
        bugfix_auth_check = 33,         // FIB-77/81/82/83: CREATE2 deploy auth check, auth failure
                                        // revert status, auth table raw-address path, auth table
                                        // squatting (merged, all activate at V3_17_0)
        bugfix_v1_error_handling = 34,  // FIB-76/78/79/80/85~92: v1 executor error-path gas
                                        // accounting, gas_left clamp on fatal error, executive
                                        // wrapper status/message marshaling (merged)
        bugfix_gas_payment_balance_precheck = 35,  // FIB-75
        bugfix_precompiled_feature_gate = 36,      // FIB-84
        bugfix_evm_storage_status = 37,            // FIB-94
        bugfix_statestorage_hash_v3_17 = 38,       // FIB-99/105
        feature_dmc2serial = 39,
        feature_sharding = 40,
        feature_rpbft = 41,
        feature_paillier = 42,
        feature_balance = 43,
        feature_balance_precompiled = 44,
        feature_balance_policy1 = 45,
        feature_paillier_add_raw = 46,
        feature_evm_eip2929 = 47,  // EIP-2929 冷热访问 gas 折扣（Berlin+）；影响全局
                                   // gas，需治理层显式启用
        feature_evm_cancun = 48,
        feature_evm_prague = 49,  // EIP-7702、BLS12-381；依赖 feature_evm_cancun
        feature_evm_osaka = 50,   // EIP-7212 p256verify、EIP-7823/EIP-7883 modexp；依赖
                                  // feature_evm_prague
        feature_evm_timestamp = 51,
        feature_evm_address = 52,
        feature_rpbft_term_weight = 53,
        feature_raw_address = 54,
        feature_rpbft_vrf_type_secp256k1 = 55,
        feature_balance_policy2 = 56,     // 转账白名单 Transfer whitelist
        feature_l2_ethereum_compat = 57,  // OP-Stack L2 mode: Ethereum-compatible
                                          // genesis/predeploys
        feature_mpt_state_root = 58,      // MPT lazy-build: block stateRoot switches to the
                                          // Ethereum MPT root from this flag's activation
                                          // block on (spec 2026-04-24 design3 4.3)
        // Takes the next unused number per the rule above, rather than the slot next to
        // bugfix_statestorage_hash_v3_17 it occupies on release-3.17.0: inserting it there would
        // renumber every feature_ flag after it and silently move their on-chain bits.
        bugfix_nonce_ordering = 59,  // web3 EOA nonce must be independent of intra-block tx order
        feature_op_jovian = 60,      // OP-Stack: Jovian fork semantics (DA footprint in
                                     // header.BlobGasUsed, operator fee ×100 formula, 17B Jovian
                                     // extraData). OFF → Isthmus semantics. Replaces the former
                                     // chain.jovian_time timestamp threshold (FISCO has no
                                     // timestamp-based fork activation); read at startup from
                                     // genesis [features], same channel as
                                     // feature_l2_ethereum_compat.
    };

    // feature_flags bit = enum value. Pin the newest flag so a value beyond
    // magic_enum's default reflection range [-128,127] is caught at compile time.
    // Values must stay CONTIGUOUS from zero: m_flags indexes by value order,
    // toFlagsNumber packs bit = enum value — a gap desyncs the two encodings.
    // (A contiguity static_assert is deliberately omitted: magic_enum only reflects
    // contiguous values by default, so `enum_max == enum_count - 1` is a tautology
    // and cannot catch gaps. The contiguous-from-zero rule is enforced by code review
    // and the comment on Flag above — the two range asserts catch the other failure
    // mode: a new flag pushed past magic_enum's reflection boundary.)
    static_assert(magic_enum::enum_contains(Flag::feature_op_jovian),
        "newest Flag fell outside magic_enum's reflection range — check enum values");
    static_assert(magic_enum::enum_integer(
                      magic_enum::enum_value<Flag>(magic_enum::enum_count<Flag>() - 1)) <= 127,
        "max Flag value exceeds 127; bitset encoding (bit = enum value) requires values 0..127.");

private:
    std::bitset<magic_enum::enum_count<Flag>()> m_flags;
    // Activation block per flag, recorded only by the readFromStorage paths (spec 5.6):
    // the shouldBuildMPT decision (PR-17) needs the block a flag activated AT, which the
    // bitset alone cannot answer. A flag enabled via bare set() has no activation context
    // and is deliberately absent here.
    std::map<Flag, protocol::BlockNumber> m_activationBlocks;

public:
    static Flag string2Flag(std::string_view str);

    static bool contains(std::string_view flag);

    void validate(std::string_view flag) const;

    void validate(Flag flag) const;

    bool get(Flag flag) const;
    bool get(std::string_view flag) const { return get(string2Flag(flag)); }

    // DO NOT use now, there is some action after set feature in systemPrecompiled
    static std::set<Flag> getFeatureDependencies(Flag flag);
    void enableDependencyFeatures(Flag flag);

    void set(Flag flag);

    void set(std::string_view flag) { set(string2Flag(flag)); }

    // The block number @p flag activated at, or -1 when unknown — i.e. the flag was never
    // loaded through a readFromStorage path (bare set() records nothing). Callers deciding
    // on activation-block semantics must check get(flag) first: an enabled-but-unrecorded
    // flag also reports -1.
    protocol::BlockNumber activationBlockOf(Flag flag) const noexcept
    {
        auto it = m_activationBlocks.find(flag);
        return it == m_activationBlocks.end() ? -1 : it->second;
    }

    // Record @p flag's activation block. Public so the free-function readFromStorage
    // overload (FeaturesStorage.h) can write it without friendship.
    void setActivationBlock(Flag flag, protocol::BlockNumber enableNumber)
    {
        m_activationBlocks[flag] = enableNumber;
    }


    void setToShardingDefault(protocol::BlockVersion version);

    void setUpgradeFeatures(protocol::BlockVersion fromVersion, protocol::BlockVersion toVersion);

    void setGenesisFeatures(protocol::BlockVersion toVersion);

    auto flags() const
    {
        return ::ranges::views::iota(0LU, m_flags.size()) |
               ::ranges::views::transform([this](size_t index) {
                   auto flag = magic_enum::enum_value<Flag>(index);
                   return std::make_tuple(flag, magic_enum::enum_name(flag), m_flags[index]);
               });
    }

    static auto featureKeys()
    {
        return ::ranges::views::iota(0LU, magic_enum::enum_count<Flag>()) |
               ::ranges::views::transform([](size_t index) {
                   auto flag = magic_enum::enum_value<Flag>(index);
                   return magic_enum::enum_name(flag);
               });
    }

    // Pack the enabled flags into a 256-bit number: bit = the flag's explicit
    // enum value (see Flag). m_flags is indexed by position in magic_enum's
    // VALUE-SORTED reflection order (not by declaration order), so map each set
    // bit back to its flag and shift by that flag's value — the encoding stays
    // stable even if flags are later grouped/reordered, as long as each flag
    // keeps its value. Consumed by L2 genesis to seed SystemConfig's
    // feature_flags entry.
    u256 toFlagsNumber() const
    {
        u256 result = 0;
        for (size_t i = 0; i < m_flags.size(); ++i)
        {
            if (m_flags[i])
            {
                result |= (u256(1) << static_cast<size_t>(magic_enum::enum_value<Flag>(i)));
            }
        }
        return result;
    }

    // Storage I/O member templates. Definitions live in FeaturesStorage.h,
    // which is included at the end of this header so existing call sites
    // (e.g. features.readFromStorage(storage, n)) keep working unchanged.
    task::Task<void> readFromStorage(
        storage2::ReadableStorage<executor_v1::StateKeyView> auto& storage, long blockNumber);

    // NOTE: second concept arg used to be executor_v1::StateValue (alias of storage::Entry).
    // Aliases cannot be forward-declared, so we use storage::Entry directly here so the
    // declaration parses with just a forward declaration of storage::Entry.
    task::Task<void> writeToStorage(
        storage2::WritableStorage<executor_v1::StateKey, storage::Entry> auto& storage,
        long blockNumber, bool ignoreDuplicate = true) const;
};

std::ostream& operator<<(std::ostream& stream, Features::Flag flag);

}  // namespace bcos::ledger
