/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "NodeConfigLoaderProbe.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigInfraLoadersTest)


BOOST_AUTO_TEST_CASE(securityConfigLegacyAndKmsError)
{
    LoaderProbe a;
    a.loadSecurityConfig(fromIni("[security]\nprivate_key_path=node.pem\nkms_type=LEGACY\n"));
    BOOST_CHECK_EQUAL(a.privateKeyPath(), "node.pem");

    LoaderProbe b;  // bad kms_type → throws
    BOOST_CHECK_THROW(
        b.loadSecurityConfig(fromIni("[security]\nkms_type=BOGUS\n")), bcos::tool::InvalidConfig);

    LoaderProbe c;  // storage_security.enable promotes LEGACY→BCOSKMS, needs url+key
    BOOST_CHECK_THROW(c.loadSecurityConfig(fromIni("[storage_security]\nenable=true\n")),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(storageSecurityConfigDisabledAndError)
{
    LoaderProbe a;  // disabled → early return
    BOOST_CHECK_NO_THROW(
        a.loadStorageSecurityConfig(fromIni("[storage_security]\nenable=false\n")));
    LoaderProbe b;  // enabled legacy without key_center_url → throws
    BOOST_CHECK_THROW(
        b.loadStorageSecurityConfig(fromIni("[storage_security]\nenable=true\nkms_type=LEGACY\n")),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(syncConfigValidAndInvalid)
{
    LoaderProbe a;
    a.loadSyncConfig(fromIni("[sync]\nsync_block_by_tree=true\ntree_width=5\n"));
    LoaderProbe b;
    BOOST_CHECK_THROW(
        b.loadSyncConfig(fromIni("[sync]\ntree_width=0\n")), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(storageConfigDefaultsAndTikv)
{
    LoaderProbe a;
    a.loadStorageConfig({});  // pure defaults — covers the bulk of the loader
    BOOST_CHECK_EQUAL(a.storageType(), "RocksDB");

    LoaderProbe b;  // TiKV branch disables separate block/state
    b.loadStorageConfig(fromIni("[storage]\ntype=TiKV\nenable_separate_block_state=true\n"));
    BOOST_CHECK(!b.enableSeparateBlockAndState());
}


BOOST_AUTO_TEST_CASE(storageConfigMptPruneValidation)
{
    // mpt_prune_window: -1 (disabled) or [1, 10000000]; anything else throws.
    LoaderProbe windowOk;
    BOOST_CHECK_NO_THROW(windowOk.loadStorageConfig(fromIni("[storage]\nmpt_prune_window=128\n")));
    BOOST_CHECK_EQUAL(windowOk.mptPruneWindow(), 128);
    for (auto const* bad : {"mpt_prune_window=0\n", "mpt_prune_window=-2\n",
             "mpt_prune_window=10000001\n"})
    {
        LoaderProbe probe;
        BOOST_CHECK_THROW(probe.loadStorageConfig(fromIni(std::string("[storage]\n") + bad)),
            bcos::tool::InvalidConfig);
    }

    // mpt_prune_sweep_garbage: bool, default false (the boot only counts and reports the
    // unreachable garbage rows).
    LoaderProbe sweepDefault;
    BOOST_CHECK_NO_THROW(sweepDefault.loadStorageConfig({}));
    BOOST_CHECK(!sweepDefault.mptPruneSweepGarbage());
    LoaderProbe sweepOn;
    BOOST_CHECK_NO_THROW(
        sweepOn.loadStorageConfig(fromIni("[storage]\nmpt_prune_sweep_garbage=true\n")));
    BOOST_CHECK(sweepOn.mptPruneSweepGarbage());
}


BOOST_AUTO_TEST_CASE(failOverConfigDisabledAndError)
{
    LoaderProbe a;  // disabled → early return
    BOOST_CHECK_NO_THROW(a.loadFailOverConfig(fromIni("[failover]\nenable=false\n"), true));
    LoaderProbe b;  // enabled, enforce member id, empty → throws
    BOOST_CHECK_THROW(b.loadFailOverConfig(fromIni("[failover]\nenable=true\n"), true),
        bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(othersConfigDefaultsAndForceSender)
{
    LoaderProbe a;
    BOOST_CHECK_NO_THROW(a.loadOthersConfig({}));
    LoaderProbe b;
    BOOST_CHECK_NO_THROW(b.loadOthersConfig(
        fromIni("[experimental]\nforce_sender=0x0000000000000000000000000000000000000001\n")));
}


BOOST_AUTO_TEST_CASE(securityConfigHsmCloudKmsBcosKms)
{
    LoaderProbe hsm;  // explicit HSM type → reads key_index (no default)
    hsm.loadSecurityConfig(
        fromIni("[security]\nkms_type=HSM\nkey_index=1\nhsm_lib_path=/tmp/x.so\npassword=p\n"));
    BOOST_CHECK_EQUAL(hsm.keyIndex(), 1);

    LoaderProbe cloud;  // CLOUDKMS with valid AWS type
    BOOST_CHECK_NO_THROW(
        cloud.loadSecurityConfig(fromIni("[security]\nkms_type=CLOUDKMS\ncloud_kms_type=AWS\n")));

    LoaderProbe cloudBad;  // CLOUDKMS with invalid type → throws
    BOOST_CHECK_THROW(
        cloudBad.loadSecurityConfig(fromIni("[security]\nkms_type=CLOUDKMS\ncloud_kms_type=ZZ\n")),
        bcos::tool::InvalidConfig);

    LoaderProbe bcoskms;  // BCOSKMS with cipher_data_key present
    BOOST_CHECK_NO_THROW(bcoskms.loadSecurityConfig(
        fromIni("[security]\nkms_type=BCOSKMS\ncipher_data_key=deadbeef\n")));

    LoaderProbe bcoskmsBad;  // BCOSKMS without cipher_data_key → throws
    BOOST_CHECK_THROW(bcoskmsBad.loadSecurityConfig(fromIni("[security]\nkms_type=BCOSKMS\n")),
        bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
