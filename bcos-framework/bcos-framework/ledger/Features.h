#pragma once
#include "../ledger/LedgerTypeDef.h"
#include "../protocol/Protocol.h"
#include "../storage/Entry.h"
#include "../storage2/Storage.h"
#include "../transaction-executor/StateKey.h"
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
        bugfix_gas_payment_balance_precheck,
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

<<<<<<< HEAD
    void setUpgradeFeatures(protocol::BlockVersion fromVersion, protocol::BlockVersion toVersion);
=======
    void setUpgradeFeatures(protocol::BlockVersion fromVersion, protocol::BlockVersion toVersion)
    {
        struct UpgradeFeatures
        {
            protocol::BlockVersion to;
            std::vector<Flag> flags;
        };
        const static auto upgradeRoadmap =
            std::to_array<UpgradeFeatures>({{.to = protocol::BlockVersion::V3_2_3_VERSION,
                                                .flags =
                                                    {
                                                        Flag::bugfix_revert,
                                                    }},
                {.to = protocol::BlockVersion::V3_2_4_VERSION,
                    .flags =
                        {
                            Flag::bugfix_statestorage_hash,
                            Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy,
                        }},
                {.to = protocol::BlockVersion::V3_2_7_VERSION,
                    .flags =
                        {
                            Flag::bugfix_event_log_order,
                            Flag::bugfix_call_noaddr_return,
                            Flag::bugfix_precompiled_codehash,
                            Flag::bugfix_dmc_revert,
                        }},
                {.to = protocol::BlockVersion::V3_5_VERSION,
                    .flags =
                        {
                            Flag::bugfix_revert,
                            Flag::bugfix_statestorage_hash,
                        }},
                {.to = protocol::BlockVersion::V3_6_VERSION,
                    .flags =
                        {
                            Flag::bugfix_statestorage_hash,
                            Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy,
                            Flag::bugfix_event_log_order,
                            Flag::bugfix_call_noaddr_return,
                            Flag::bugfix_precompiled_codehash,
                            Flag::bugfix_dmc_revert,
                        }},
                {.to = protocol::BlockVersion::V3_6_1_VERSION,
                    .flags =
                        {
                            Flag::bugfix_keypage_system_entry_hash,
                            Flag::bugfix_internal_create_redundant_storage,
                        }},
                {.to = protocol::BlockVersion::V3_7_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_empty_abi_reset,
                            Flag::bugfix_eip55_addr,
                            Flag::bugfix_sharding_call_in_child_executive,
                            Flag::bugfix_internal_create_permission_denied,
                        }},
                {.to = protocol::BlockVersion::V3_8_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_eoa_as_contract,
                            Flag::bugfix_dmc_deploy_gas_used,
                            Flag::bugfix_evm_exception_gas_used,
                            Flag::bugfix_set_row_with_dirty_flag,
                        }},
                {.to = protocol::BlockVersion::V3_9_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_staticcall_noaddr_return,
                            Flag::bugfix_support_transfer_receive_fallback,
                            Flag::bugfix_eoa_match_failed,
                        }},
                {.to = protocol::BlockVersion::V3_12_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_rpbft_vrf_blocknumber_input,
                        }},
                {.to = protocol::BlockVersion::V3_13_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_delete_account_code,
                            Flag::bugfix_policy1_empty_code_address,
                            Flag::bugfix_precompiled_gasused,
                        }},
                {.to = protocol::BlockVersion::V3_14_0_VERSION,
                    .flags =
                        {
                            Flag::bugfix_nonce_not_increase_when_revert,
                            Flag::bugfix_set_contract_nonce_when_create,
                        }},
                {.to = protocol::BlockVersion::V3_15_1_VERSION,
                    .flags = {Flag::bugfix_precompiled_gascalc}},
                {.to = protocol::BlockVersion::V3_15_2_VERSION,
                    .flags =
                        {
                            Flag::bugfix_method_auth_sender,
                            Flag::bugfix_precompiled_evm_status,
                        }},
                {.to = protocol::BlockVersion::V3_16_0_VERSION,
                    .flags = {Flag::bugfix_delegatecall_transfer, Flag::bugfix_nonce_initialize,
                        Flag::bugfix_v1_timestamp}},
                {.to = protocol::BlockVersion::V3_16_4_VERSION,
                    .flags = {Flag::bugfix_revert_logs}},
                {.to = protocol::BlockVersion::V3_16_5_VERSION,
                    .flags =
                        {
                            Flag::bugfix_auth_check_create2,
                            Flag::bugfix_auth_check_revert_status,
                            Flag::bugfix_auth_table_raw_address,
                            Flag::bugfix_auth_table_squatting,
                        }},
                {.to = protocol::BlockVersion::V3_17_0_VERSION,
                    .flags = {
                        Flag::bugfix_statestorage_hash_v3_17,
                    }}});
        for (const auto& upgradeFeatures : upgradeRoadmap)
        {
            if (((toVersion < protocol::BlockVersion::V3_2_7_VERSION) &&
                    (toVersion >= upgradeFeatures.to)) ||
                (fromVersion < upgradeFeatures.to && toVersion >= upgradeFeatures.to))
            {
                for (auto flag : upgradeFeatures.flags)
                {
                    set(flag);
                }
            }
        }
    }
>>>>>>> 438c82d82 (refactor(storage): gate FIB-99 hash via Features flag, not block version)

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

    task::Task<void> writeToStorage(
        storage2::WritableStorage<executor_v1::StateKey, executor_v1::StateValue> auto& storage,
        long blockNumber, bool ignoreDuplicate = true) const;
};

std::ostream& operator<<(std::ostream& stream, Features::Flag flag);

}  // namespace bcos::ledger
