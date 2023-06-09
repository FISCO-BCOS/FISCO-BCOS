#include <bcos-framework/ledger/Features.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos::ledger;

struct FeaturesTestFixture
{
};

BOOST_FIXTURE_TEST_SUITE(FeaturesTest, FeaturesTestFixture)

BOOST_AUTO_TEST_CASE(feature)
{
    Features features1;
    BOOST_CHECK_EQUAL(features1.get(Features::Flag::FEATURE_BUGFIX_REVERT), false);
    BOOST_CHECK_EQUAL(features1.get("FEATURE_BUGFIX_REVERT"), false);

    features1.set("FEATURE_BUGFIX_REVERT");
    BOOST_CHECK_EQUAL(features1.get(Features::Flag::FEATURE_BUGFIX_REVERT), true);
    BOOST_CHECK_EQUAL(features1.get("FEATURE_BUGFIX_REVERT"), true);

    Features features2;
    BOOST_CHECK_EQUAL(features2.get(Features::Flag::FEATURE_BUGFIX_REVERT), false);
    BOOST_CHECK_EQUAL(features2.get("FEATURE_BUGFIX_REVERT"), false);

    features2.set(Features::Flag::FEATURE_BUGFIX_REVERT);
    BOOST_CHECK_EQUAL(features2.get(Features::Flag::FEATURE_BUGFIX_REVERT), true);
    BOOST_CHECK_EQUAL(features2.get("FEATURE_BUGFIX_REVERT"), true);

    Features features3;
    features3.setToDefault();
    auto flags = features3.flags();
    auto [flag, name, value] = flags[0];
    BOOST_CHECK_EQUAL(flag, Features::Flag::FEATURE_BUGFIX_REVERT);
    BOOST_CHECK_EQUAL(name, "FEATURE_BUGFIX_REVERT");
    BOOST_CHECK_EQUAL(value, true);
}

BOOST_AUTO_TEST_SUITE_END()