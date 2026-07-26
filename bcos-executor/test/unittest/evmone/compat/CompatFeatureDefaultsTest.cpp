/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-F*: new EVM feature flags default off on fresh Features().
 *  @file CompatFeatureDefaultsTest.cpp
 */

#include "bcos-framework/ledger/Features.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatFeatureDefaults)

// FC_F_eip2929_default_off: removed — feature_evm_eip2929 flag was removed,
// EIP-2929 now follows EVM revision directly.
BOOST_AUTO_TEST_CASE(FC_F_eip2929_default_off)
{
    // Placeholder test — flag no longer exists.
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(FC_F_prague_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_prague));
}

BOOST_AUTO_TEST_CASE(FC_F_osaka_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_osaka));
}

BOOST_AUTO_TEST_CASE(FC_F_cancun_default_off)
{
    ledger::Features f;
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_cancun));
}

BOOST_AUTO_TEST_CASE(FC_F_flags_contain_string)
{
    BOOST_CHECK(ledger::Features::contains("feature_evm_cancun"));
    BOOST_CHECK(ledger::Features::contains("feature_evm_prague"));
    BOOST_CHECK(ledger::Features::contains("feature_evm_osaka"));
    // feature_evm_eip2929: flag removed, EIP-2929 follows EVM revision
}

BOOST_AUTO_TEST_CASE(FC_F_prague_does_not_auto_enable_cancun)
{
    ledger::Features f;
    f.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK(!f.get(ledger::Features::Flag::feature_evm_cancun));
}

BOOST_AUTO_TEST_SUITE_END()  // CompatFeatureDefaults
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
