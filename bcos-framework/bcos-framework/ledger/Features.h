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
    // Flags persist by NAME only (SYS_CONFIG keys and the genesis pin), so never rename one.
    // A retired flag keeps its name while existing genesis files may still carry it.
    enum class Flag
    {
        bugfix_revert,  // https://github.com/FISCO-BCOS/FISCO-BCOS/issues/3629
        bugfix_statestorage_hash,
        bugfix_evm_create2_delegatecall_staticcall_codecopy,
        bugfix_event_log_order,
        bugfix_call_noaddr_return,
        bugfix_precompiled_codehash,
        bugfix_dmc_revert,
        bugfix_keypage_system_entry_hash,
        bugfix_internal_create_redundant_storage,  // perf internal create code and abi storage
        bugfix_internal_create_permission_denied,
        bugfix_sharding_call_in_child_executive,
        bugfix_empty_abi_reset,  // support empty abi reset of same code
        bugfix_eip55_addr,
        bugfix_eoa_as_contract,
        bugfix_eoa_match_failed,
        bugfix_evm_exception_gas_used,
        bugfix_dmc_deploy_gas_used,
        bugfix_staticcall_noaddr_return,
        bugfix_support_transfer_receive_fallback,
        bugfix_set_row_with_dirty_flag,
        bugfix_rpbft_vrf_blocknumber_input,
        bugfix_delete_account_code,
        bugfix_policy1_empty_code_address,
        bugfix_precompiled_gasused,
        bugfix_nonce_not_increase_when_revert,
        bugfix_set_contract_nonce_when_create,
        bugfix_precompiled_gascalc,
        bugfix_method_auth_sender,
        bugfix_precompiled_evm_status,
        bugfix_delegatecall_transfer,
        bugfix_nonce_initialize,
        bugfix_v1_timestamp,
        bugfix_revert_logs,
        bugfix_auth_check,         // FIB-77/81/82/83: CREATE2 deploy auth check, auth failure
                                   // revert status, auth table raw-address path, auth table
                                   // squatting (merged, all activate at V3_17_0)
        bugfix_v1_error_handling,  // FIB-76/78/79/80/85~92: v1 executor error-path gas
                                   // accounting, gas_left clamp on fatal error, executive
                                   // wrapper status/message marshaling (merged)
        bugfix_gas_payment_balance_precheck,  // FIB-75
        bugfix_precompiled_feature_gate,      // FIB-84
        bugfix_evm_storage_status,            // FIB-94
        bugfix_statestorage_hash_v3_17,       // FIB-99/105
        feature_dmc2serial,
        feature_sharding,
        feature_rpbft,
        feature_paillier,
        feature_balance,
        feature_balance_precompiled,
        feature_balance_policy1,
        feature_paillier_add_raw,
        feature_evm_eip2929,  // EIP-2929 冷热访问 gas 折扣（Berlin+）；影响全局
                              // gas，需治理层显式启用
        feature_evm_cancun,
        feature_evm_prague,  // EIP-7702、BLS12-381；依赖 feature_evm_cancun
        feature_evm_osaka,   // EIP-7212 p256verify、EIP-7823/EIP-7883 modexp；依赖
                             // feature_evm_prague
        feature_evm_timestamp,
        feature_evm_address,
        feature_rpbft_term_weight,
        // DEPRECATED: the account-table encoding (hex vs binary /apps/ names) is a
        // node-local physical layout, published by the startup detection as
        // ledger::account::nodeAddressTableMode() (ledger/AccountTableName.h) — this flag no
        // longer drives any behavior. string2Flag still recognizes the name so existing
        // config.genesis files load; setting it, through governance or config.genesis, is
        // accepted with a warning and records an inert no-op row.
        feature_raw_address,
        feature_rpbft_vrf_type_secp256k1,
        feature_balance_policy2,  // 转账白名单 Transfer whitelist
        feature_mpt_state_root,   // MPT lazy-build: block stateRoot switches to the
                                  // Ethereum MPT root from this flag's activation
                                  // block on (spec 2026-04-24 design3 4.3)
        bugfix_nonce_ordering,    // web3 EOA nonce must be independent of intra-block tx order
        bugfix_eip161_1052_account_semantics,  // #5371/#5372: v1 executor answers
                                               // account_exists per EIP-161 (empty == absent)
                                               // and EXTCODEHASH per EIP-1052: the chain
                                               // hasher's hash("") (keccak on a non-SM
                                               // chain) for a code-less live account, 0 for
                                               // an absent/empty one; was true / 0.
        bugfix_evm2929_created_address_warm,   // Chains with feature_evm_eip2929, v1 lane:
                                               // evmone 0.24's create_impl warms the created
                                               // address (access_account on msg.recipient);
                                               // 0.21 never called it, so on the base the
                                               // address stayed cold until its first genuine
                                               // touch. ON keeps the 0.24/geth behaviour; OFF
                                               // reproduces the base metering exactly: the
                                               // creation-time query answers cold without
                                               // inserting, and the first genuine touch pays
                                               // the cold fee and turns the address warm.
                                               // v2 OFF retains historical creation pins
                                               // across creator rollback; ON journals
                                               // creation warmth in the creator's scope.
    };

    // magic_enum reflects values in its default range [-128, 127]; a flag past it would drop out
    // of get/set/string2Flag.
    static_assert(magic_enum::enum_count<Flag>() <= 128,
        "Flag count exceeds magic_enum's default reflection range");

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

    /// Rejects a feature the governance setSystemConfig path must not turn on: one whose
    /// dependencies (getFeatureDependencies) are not all enabled yet.
    void validate(Flag flag) const;

    /// Throws InvalidSetFeature naming the first dependency of @p flag that is not enabled.
    /// Ledger::buildGenesisBlock calls it on first init of a compatibility_version >= 3.18.0
    /// genesis; validate() calls it per flag.
    void validateDependencies(Flag flag) const;

    /// A deprecated flag is still parsed (string2Flag recognizes the name) but drives
    /// nothing. Entry points accept it with a warning
    /// rather than reject it — rejecting would fail governance transactions and
    /// config.genesis files written before the deprecation, while the flag is an inert
    /// no-op either way. validate() warns on the governance path;
    /// NodeConfig::loadGenesisFeatures warns on the genesis path.
    static constexpr bool isDeprecated(Flag flag) noexcept
    {
        return flag == Flag::feature_raw_address;
    }

    bool get(Flag flag) const;
    bool get(std::string_view flag) const { return get(string2Flag(flag)); }

    /// The flags that must already be enabled before @p flag can be.
    static std::set<Flag> getFeatureDependencies(Flag flag);

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
