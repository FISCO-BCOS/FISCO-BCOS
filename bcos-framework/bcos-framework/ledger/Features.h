#pragma once
#include "../protocol/Protocol.h"
#include "../storage2/Storage.h"
#include "bcos-task/Task.h"
#include <bcos-utilities/Exceptions.h>
#include <array>
#include <bitset>
#include <magic_enum/magic_enum.hpp>
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
    // At most 256 flag
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
        bugfix_internal_create_redundant_storage,  // to perf internal create code and abi storage
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
        bugfix_auth_check_create2,
        bugfix_auth_check_revert_status,
        bugfix_auth_table_raw_address,
        bugfix_auth_table_squatting,
        bugfix_v1_exec_error_gas_used,
        bugfix_v1_precompiled_error_gas,      // FIB-76/79/80: precompiled gas overflow check,
                                              // exception safety, and use remaining gas on revert
        bugfix_gas_payment_balance_precheck,  // FIB-75
        bugfix_clamp_gas_left_on_error,       // FIB-78
        bugfix_precompiled_feature_gate,      // FIB-84
        bugfix_v1_executive_wrapper,          // FIB-85/86/87
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
        feature_evm_cancun,
        feature_evm_timestamp,
        feature_evm_address,
        feature_rpbft_term_weight,
        feature_raw_address,
        feature_rpbft_vrf_type_secp256k1,
        feature_balance_policy2,  // 转账白名单 Transfer whitelist
    };

private:
    std::bitset<magic_enum::enum_count<Flag>()> m_flags;

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
