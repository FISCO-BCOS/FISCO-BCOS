#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "protocol/Protocol.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::ledger;

struct FeaturesTestFixture
{
};

BOOST_FIXTURE_TEST_SUITE(FeaturesTest, FeaturesTestFixture)

BOOST_AUTO_TEST_CASE(feature)
{
    Features features1;
    BOOST_CHECK_EQUAL(features1.get(Features::Flag::bugfix_revert), false);
    BOOST_CHECK_EQUAL(features1.get("bugfix_revert"), false);

    features1.set("bugfix_revert");
    BOOST_CHECK_EQUAL(features1.get(Features::Flag::bugfix_revert), true);
    BOOST_CHECK_EQUAL(features1.get("bugfix_revert"), true);

    Features features2;
    BOOST_CHECK_EQUAL(features2.get(Features::Flag::bugfix_revert), false);
    BOOST_CHECK_EQUAL(features2.get("bugfix_revert"), false);

    features2.set(Features::Flag::bugfix_revert);
    BOOST_CHECK_EQUAL(features2.get(Features::Flag::bugfix_revert), true);
    BOOST_CHECK_EQUAL(features2.get("bugfix_revert"), true);

    Features features3;
    features3.setGenesisFeatures(bcos::protocol::BlockVersion::V3_2_VERSION);
    auto flags = features3.flags();
    bcos::ledger::Features::Flag flag{};
    std::string_view name;
    bool value{};

    flag = std::get<0>(flags[0]);
    name = std::get<1>(flags[0]);
    value = std::get<2>(flags[0]);
    BOOST_CHECK_EQUAL(flag, Features::Flag::bugfix_revert);
    BOOST_CHECK_EQUAL(name, "bugfix_revert");
    BOOST_CHECK_EQUAL(value, false);

    features3.setGenesisFeatures(bcos::protocol::BlockVersion::V3_2_3_VERSION);
    flags = features3.flags();
    flag = std::get<0>(flags[0]);
    name = std::get<1>(flags[0]);
    value = std::get<2>(flags[0]);

    BOOST_CHECK_EQUAL(flag, Features::Flag::bugfix_revert);
    BOOST_CHECK_EQUAL(name, "bugfix_revert");
    BOOST_CHECK_EQUAL(value, true);

    BOOST_CHECK_EQUAL(features3.get(Features::Flag::feature_dmc2serial), false);
    BOOST_CHECK_EQUAL(features3.get("feature_dmc2serial"), false);

    BOOST_CHECK_EQUAL(features3.get(Features::Flag::feature_balance), false);
    BOOST_CHECK_EQUAL(features3.get("feature_balance"), false);

    BOOST_CHECK_EQUAL(features3.get(Features::Flag::feature_balance_precompiled), false);
    BOOST_CHECK_EQUAL(features3.get("feature_balance_precompiled"), false);

    BOOST_CHECK_EQUAL(features3.get(Features::Flag::feature_balance_policy1), false);
    BOOST_CHECK_EQUAL(features3.get("feature_balance_policy1"), false);

    Features features4;
    BOOST_CHECK_EQUAL(features4.get(Features::Flag::feature_dmc2serial), false);
    BOOST_CHECK_EQUAL(features4.get("feature_dmc2serial"), false);

    features4.set("feature_dmc2serial");
    BOOST_CHECK_EQUAL(features4.get(Features::Flag::feature_dmc2serial), true);
    BOOST_CHECK_EQUAL(features4.get("feature_dmc2serial"), true);

    Features features5;
    BOOST_CHECK_EQUAL(features5.get(Features::Flag::feature_dmc2serial), false);
    BOOST_CHECK_EQUAL(features5.get("feature_dmc2serial"), false);

    features5.set(Features::Flag::feature_dmc2serial);
    BOOST_CHECK_EQUAL(features5.get(Features::Flag::feature_dmc2serial), true);
    BOOST_CHECK_EQUAL(features5.get("feature_dmc2serial"), true);

    Features features6;
    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance), false);
    BOOST_CHECK_EQUAL(features6.get("feature_balance"), false);

    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance_precompiled), false);
    BOOST_CHECK_EQUAL(features6.get("feature_balance_precompiled"), false);

    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance_policy1), false);
    BOOST_CHECK_EQUAL(features6.get("feature_balance_policy1"), false);

    features6.set("feature_balance");
    features6.set("feature_balance_precompiled");
    features6.set("feature_balance_policy1");
    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance), true);
    BOOST_CHECK_EQUAL(features6.get("feature_balance"), true);

    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance_precompiled), true);
    BOOST_CHECK_EQUAL(features6.get("feature_balance_precompiled"), true);

    BOOST_CHECK_EQUAL(features6.get(Features::Flag::feature_balance_policy1), true);
    BOOST_CHECK_EQUAL(features6.get("feature_balance_policy1"), true);

    Features features7;
    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance), false);
    BOOST_CHECK_EQUAL(features7.get("feature_balance"), false);

    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance_precompiled), false);
    BOOST_CHECK_EQUAL(features7.get("feature_balance_precompiled"), false);

    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance_policy1), false);
    BOOST_CHECK_EQUAL(features7.get("feature_balance_policy1"), false);

    features7.set(Features::Flag::feature_balance);
    features7.set(Features::Flag::feature_balance_precompiled);
    features7.set(Features::Flag::feature_balance_policy1);
    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance), true);
    BOOST_CHECK_EQUAL(features7.get("feature_balance"), true);

    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance_precompiled), true);
    BOOST_CHECK_EQUAL(features7.get("feature_balance_precompiled"), true);

    BOOST_CHECK_EQUAL(features7.get(Features::Flag::feature_balance_policy1), true);
    BOOST_CHECK_EQUAL(features7.get("feature_balance_policy1"), true);

    auto keys = Features::featureKeys();
    // clang-format off
    std::vector<std::string> compareKeys = {
        "bugfix_revert",
        "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy",
        "bugfix_event_log_order",
        "bugfix_call_noaddr_return",
        "bugfix_precompiled_codehash",
        "bugfix_dmc_revert",
        "bugfix_keypage_system_entry_hash",
        "bugfix_internal_create_redundant_storage",
        "bugfix_internal_create_permission_denied",
        "bugfix_sharding_call_in_child_executive",
        "bugfix_empty_abi_reset",
        "bugfix_eip55_addr",
        "bugfix_eoa_as_contract",
        "bugfix_eoa_match_failed",
        "bugfix_evm_exception_gas_used",
        "bugfix_dmc_deploy_gas_used",
        "bugfix_staticcall_noaddr_return",
        "bugfix_support_transfer_receive_fallback",
        "bugfix_set_row_with_dirty_flag",
        "feature_dmc2serial",
        "feature_sharding",
        "feature_rpbft",
        "feature_paillier",
        "feature_balance",
        "feature_balance_precompiled",
        "feature_balance_policy1",
        "feature_paillier_add_raw",
        "feature_evm_cancun"
    };
    // clang-format on
    for (size_t i = 0; i < keys.size(); ++i)
    {
        BOOST_CHECK_EQUAL(keys[i], compareKeys[i]);
    }
}

auto validFlags(const Features& features)
{
    return features.flags() |
           RANGES::views::filter([](auto feature) { return std::get<2>(feature); }) |
           RANGES::to<std::vector>();
}

BOOST_AUTO_TEST_CASE(upgrade)
{
    // 3.2 to 3.2.7
    Features features1;
    features1.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_VERSION, bcos::protocol::BlockVersion::V3_2_7_VERSION);
    auto expect1 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy", "bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert"});
    auto flags1 = validFlags(features1);
    BOOST_CHECK_EQUAL(flags1.size(), expect1.size());
    for (auto feature : expect1)
    {
        BOOST_CHECK(features1.get(feature));
    }

    // 3.2.7 to 3.3
    Features features2;
    features2.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_7_VERSION, bcos::protocol::BlockVersion::V3_3_VERSION);
    BOOST_CHECK(validFlags(features2).empty());

    // 3.3 to 3.4
    Features features3;
    features3.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_3_VERSION, bcos::protocol::BlockVersion::V3_4_VERSION);
    BOOST_CHECK(validFlags(features3).empty());

    // 3.2.7 to 3.6.0
    Features features4;
    features4.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_7_VERSION, bcos::protocol::BlockVersion::V3_6_VERSION);
    auto expect4 =
        std::to_array<std::string_view>({"bugfix_event_log_order", "bugfix_call_noaddr_return",
            "bugfix_precompiled_codehash", "bugfix_revert", "bugfix_dmc_revert",
            "bugfix_statestorage_hash", "bugfix_evm_create2_delegatecall_staticcall_codecopy"});
    BOOST_CHECK_EQUAL(validFlags(features4).size(), expect4.size());
    for (auto feature : expect4)
    {
        BOOST_CHECK(features4.get(feature));
    }

    // 3.2.4 to 3.6.0
    Features features5;
    features5.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_4_VERSION, bcos::protocol::BlockVersion::V3_6_VERSION);
    auto expect2 =
        std::to_array<std::string_view>({"bugfix_event_log_order", "bugfix_call_noaddr_return",
            "bugfix_precompiled_codehash", "bugfix_revert", "bugfix_dmc_revert",
            "bugfix_statestorage_hash", "bugfix_evm_create2_delegatecall_staticcall_codecopy"});
    BOOST_CHECK_EQUAL(validFlags(features5).size(), expect2.size());
    for (auto feature : expect2)
    {
        BOOST_CHECK(features5.get(feature));
    }

    // 3.4 to 3.5
    Features features6;
    features6.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_4_VERSION, bcos::protocol::BlockVersion::V3_5_VERSION);
    auto expect3 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash"});
    BOOST_CHECK_EQUAL(validFlags(features6).size(), expect3.size());
    for (auto feature : expect3)
    {
        BOOST_CHECK(features6.get(feature));
    }

    // 3.2.2 to 3.2.3
    Features features7;
    features7.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_2_VERSION, bcos::protocol::BlockVersion::V3_2_3_VERSION);
    auto expect5 = std::to_array<std::string_view>({"bugfix_revert"});
    BOOST_CHECK_EQUAL(validFlags(features7).size(), expect5.size());
    for (auto feature : expect5)
    {
        BOOST_CHECK(features7.get(feature));
    }

    // 3.2.5 to 3.2.7
    Features features8;
    features8.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_5_VERSION, bcos::protocol::BlockVersion::V3_2_7_VERSION);
    auto expect6 = std::to_array<std::string_view>({"bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert"});
    BOOST_CHECK_EQUAL(validFlags(features8).size(), expect6.size());
    for (auto feature : expect6)
    {
        BOOST_CHECK(features8.get(feature));
    }

    // 3.5.0 to 3.6.0
    Features features9;
    features9.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_5_VERSION, bcos::protocol::BlockVersion::V3_6_VERSION);
    auto expect7 = std::to_array<std::string_view>({"bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy", "bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert"});
    BOOST_CHECK_EQUAL(validFlags(features9).size(), expect7.size());
    for (auto feature : expect7)
    {
        BOOST_CHECK(features9.get(feature));
    }

    // 3.4.0  to 3.5.0
    Features features10;
    features10.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_4_VERSION, bcos::protocol::BlockVersion::V3_5_VERSION);
    auto expect8 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash"});
    BOOST_CHECK_EQUAL(validFlags(features10).size(), expect8.size());
    for (auto feature : expect8)
    {
        BOOST_CHECK(features10.get(feature));
    }

    // 3.6.0 to 3.7.0
    Features features11;
    features11.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_6_VERSION, bcos::protocol::BlockVersion::V3_7_0_VERSION);
    auto expect9 = std::to_array<std::string_view>({"bugfix_keypage_system_entry_hash",
        "bugfix_internal_create_redundant_storage", "bugfix_empty_abi_reset", "bugfix_eip55_addr",
        "bugfix_sharding_call_in_child_executive", "bugfix_internal_create_permission_denied"});
    BOOST_CHECK_EQUAL(validFlags(features11).size(), expect9.size());
    for (auto feature : expect9)
    {
        BOOST_CHECK(features11.get(feature));
    }

    // 3.2.4 to 3.2.6
    Features features12;
    features12.setUpgradeFeatures(
        bcos::protocol::BlockVersion::V3_2_4_VERSION, bcos::protocol::BlockVersion::V3_2_6_VERSION);
    auto expect10 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy"});
    BOOST_CHECK_EQUAL(validFlags(features12).size(), expect10.size());
    for (auto feature : expect10)
    {
        BOOST_CHECK(features12.get(feature));
    }
}

BOOST_AUTO_TEST_CASE(genesis)
{
    // 3.2.4
    Features features1;
    features1.setGenesisFeatures(bcos::protocol::BlockVersion::V3_2_4_VERSION);
    auto expect1 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy"});
    BOOST_CHECK_EQUAL(validFlags(features1).size(), expect1.size());
    for (auto feature : expect1)
    {
        BOOST_CHECK(features1.get(feature));
    }

    // 3.5.0
    Features features2;
    features2.setGenesisFeatures(bcos::protocol::BlockVersion::V3_5_VERSION);
    auto expect2 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash"});
    BOOST_CHECK_EQUAL(validFlags(features2).size(), expect2.size());
    for (auto feature : expect2)
    {
        BOOST_CHECK(features2.get(feature));
    }

    // 3.6.0
    Features features3;
    features3.setGenesisFeatures(bcos::protocol::BlockVersion::V3_6_VERSION);
    auto expect3 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy", "bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert"});
    BOOST_CHECK_EQUAL(validFlags(features3).size(), expect3.size());
    for (auto feature : expect3)
    {
        BOOST_CHECK(features3.get(feature));
    }

    // 3.7.0
    Features features37;
    features37.setGenesisFeatures(bcos::protocol::BlockVersion::V3_7_0_VERSION);
    auto expect37 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy", "bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert",
        "bugfix_keypage_system_entry_hash", "bugfix_internal_create_redundant_storage",
        "bugfix_empty_abi_reset", "bugfix_eip55_addr", "bugfix_sharding_call_in_child_executive",
        "bugfix_internal_create_permission_denied"});
    BOOST_CHECK_EQUAL(validFlags(features37).size(), expect37.size());
    for (auto feature : expect37)
    {
        BOOST_CHECK(features37.get(feature));
    }

    // 3.8.0
    Features features38;
    features38.setGenesisFeatures(bcos::protocol::BlockVersion::V3_8_0_VERSION);
    auto expect38 = std::to_array<std::string_view>({"bugfix_revert", "bugfix_statestorage_hash",
        "bugfix_evm_create2_delegatecall_staticcall_codecopy", "bugfix_event_log_order",
        "bugfix_call_noaddr_return", "bugfix_precompiled_codehash", "bugfix_dmc_revert",
        "bugfix_keypage_system_entry_hash", "bugfix_internal_create_redundant_storage",
        "bugfix_empty_abi_reset", "bugfix_eip55_addr", "bugfix_sharding_call_in_child_executive",
        "bugfix_internal_create_permission_denied", "bugfix_eoa_as_contract",
        "bugfix_dmc_deploy_gas_used", "bugfix_evm_exception_gas_used",
        "bugfix_set_row_with_dirty_flag"});

    BOOST_CHECK_EQUAL(validFlags(features38).size(), expect38.size());
    for (auto feature : expect38)
    {
        BOOST_CHECK(features38.get(feature));
    }

    // 3.4.0
    Features features4;
    features4.setGenesisFeatures(bcos::protocol::BlockVersion::V3_4_VERSION);
    auto expect4 = std::to_array<std::string_view>({"feature_sharding"});
    BOOST_CHECK_EQUAL(validFlags(features4).size(), expect4.size());
    for (auto feature : expect4)
    {
        BOOST_CHECK(features4.get(feature));
    }
}

BOOST_AUTO_TEST_SUITE_END()
