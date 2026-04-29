#include "bcos-framework/ledger/Features.h"

#include "bcos-tool/Exceptions.h"
#include <boost/core/ignore_unused.hpp>
#include <boost/throw_exception.hpp>

namespace bcos::ledger
{
Features::Flag Features::string2Flag(std::string_view str)
{
    auto value = magic_enum::enum_cast<Flag>(str);
    if (!value)
    {
        BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
    }
    return *value;
}

bool Features::contains(std::string_view flag)
{
    return magic_enum::enum_cast<Flag>(flag).has_value();
}

void Features::validate(std::string_view flag) const
{
    auto value = magic_enum::enum_cast<Flag>(flag);
    if (!value)
    {
        BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
    }

    validate(*value);
}

void Features::validate(Flag flag) const
{
    if (flag == Flag::feature_balance_precompiled && !get(Flag::feature_balance))
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidSetFeature{} << errinfo_comment("must set feature_balance first"));
    }
    if (flag == Flag::feature_balance_policy1 && !get(Flag::feature_balance_precompiled))
    {
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidSetFeature{}
                              << errinfo_comment("must set feature_balance_precompiled first"));
    }
}

bool Features::get(Flag flag) const
{
    auto index = magic_enum::enum_index(flag);
    if (!index)
    {
        BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
    }

    return m_flags[*index];
}

std::set<Features::Flag> Features::getFeatureDependencies(Flag flag)
{
    boost::ignore_unused(flag);
    return {};
}

void Features::enableDependencyFeatures(Flag flag)
{
    for (const auto& dependence : getFeatureDependencies(flag))
    {
        set(dependence);
    }
}

void Features::set(Flag flag)
{
    auto index = magic_enum::enum_index(flag);
    if (!index)
    {
        BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
    }

    m_flags[*index] = true;
}

void Features::setToShardingDefault(protocol::BlockVersion version)
{
    if (version >= protocol::BlockVersion::V3_3_VERSION &&
        version <= protocol::BlockVersion::V3_4_VERSION)
    {
        set(Flag::feature_sharding);
    }
}

void Features::setUpgradeFeatures(
    protocol::BlockVersion fromVersion, protocol::BlockVersion toVersion)
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
            {.to = protocol::BlockVersion::V3_16_4_VERSION, .flags = {Flag::bugfix_revert_logs}},
            {.to = protocol::BlockVersion::V3_16_5_VERSION,
                .flags = {
                    Flag::bugfix_auth_check_create2,
                    Flag::bugfix_auth_check_revert_status,
                    Flag::bugfix_auth_table_raw_address,
                    Flag::bugfix_auth_table_squatting,
                    Flag::bugfix_v1_exec_error_gas_used,
                    Flag::bugfix_v1_precompiled_error_gas,
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

void Features::setGenesisFeatures(protocol::BlockVersion toVersion)
{
    setToShardingDefault(toVersion);
    if (toVersion == protocol::BlockVersion::V3_3_VERSION ||
        toVersion == protocol::BlockVersion::V3_4_VERSION)
    {
        return;
    }

    if (toVersion == protocol::BlockVersion::V3_5_VERSION)
    {
        setUpgradeFeatures(protocol::BlockVersion::V3_4_VERSION, toVersion);
    }
    else
    {
        setUpgradeFeatures(protocol::BlockVersion::MIN_VERSION, toVersion);
    }
}

std::ostream& operator<<(std::ostream& stream, Features::Flag flag)
{
    stream << magic_enum::enum_name(flag);
    return stream;
}
}  // namespace bcos::ledger