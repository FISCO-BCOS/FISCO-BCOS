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
    features3.setToDefault(bcos::protocol::BlockVersion::V3_2_VERSION);
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

    features3.setToDefault(bcos::protocol::BlockVersion::V3_2_3_VERSION);
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
        "feature_dmc2serial",
        "feature_sharding",
        "feature_rpbft",
        "feature_paillier",
        "feature_balance",
        "feature_balance_precompiled",
        "feature_balance_policy1",
        "feature_paillier_add_raw"
    };
    // clang-format on
    for (size_t i = 0; i < keys.size(); ++i)
    {
        BOOST_CHECK_EQUAL(keys[i], compareKeys[i]);
    }
}

BOOST_AUTO_TEST_SUITE_END()
