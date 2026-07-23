/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file test_NodeConfigAllocs.cpp
 * @brief [alloc.N] / [alloc.N.storage] parsing gated by feature_l2_ethereum_compat (A6.5)
 */
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <sstream>
#include <string>

using namespace bcos::tool;

namespace
{
boost::property_tree::ptree parseIni(std::string const& content)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(content);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}

std::shared_ptr<NodeConfig> makeNodeConfig()
{
    return std::make_shared<NodeConfig>(std::make_shared<bcos::crypto::KeyFactoryImpl>());
}

// loadGenesisConfig -> loadLedgerConfig needs a non-empty sealer list and a
// KeyFactory to build node ids; allocs/invariant checks run after that, so the
// base config must carry one node line. There is no [chain].chain_mode anymore —
// L2 mode is signalled by feature_l2_ethereum_compat in [features], so L2 cases
// append kFeatureL2.
constexpr auto kBase =
    "[chain]\nsm_crypto=0\nchain_id=1\ngroup_id=g\n"
    "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
    "node.0=0102030405060708090a0b0c0d0e0f1011121314:1\n"
    "[version]\ncompatibility_version=3.10.0\n"
    "[tx]\ngas_limit=300000000\n"
    "[executor]\nis_auth_check=1\nauth_admin_account=0x0\n";

constexpr auto kFeatureL2 = "[features]\nfeature_l2_ethereum_compat=1\n";

constexpr auto kAlloc0 =
    "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
    "balance=0\nnonce=0\ncode=0x6080604052\n";
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigAllocsTest)

BOOST_AUTO_TEST_CASE(DefaultNoFeatureNoAllocs)
{
    auto cfg = makeNodeConfig();
    cfg->loadGenesisConfig(parseIni(kBase));
    BOOST_CHECK(cfg->genesisConfig().m_allocs.empty());
}

BOOST_AUTO_TEST_CASE(FeatureL2WithAllocsParsed)
{
    auto cfg = makeNodeConfig();
    cfg->loadGenesisConfig(parseIni(std::string(kBase) + kFeatureL2 + kAlloc0));
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs.size(), 1U);
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs[0].address,
        "0x42000000000000000000000000000000000000c0");  // forced lowercase
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs[0].code, "0x6080604052");
}

BOOST_AUTO_TEST_CASE(FeatureL2RejectsEmptyAllocs)
{
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(std::string(kBase) + kFeatureL2)),
        bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(AllocsWithoutFeatureRejected)
{
    // allocs present but feature_l2_ethereum_compat not enabled -> reject
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(
        cfg->loadGenesisConfig(parseIni(std::string(kBase) + kAlloc0)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(AllocStorageSlotsParsed)
{
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.0.storage]\n"
                      "0x0000000000000000000000000000000000000000000000000000000000000000="
                      "0x0000000000000000000000000000000000000000000000000000000000000001\n";
    auto cfg = makeNodeConfig();
    cfg->loadGenesisConfig(parseIni(ini));
    auto const& allocs = cfg->genesisConfig().m_allocs;
    BOOST_CHECK_EQUAL(allocs.size(), 1U);
    BOOST_CHECK_EQUAL(allocs[0].storage.size(), 1U);
    BOOST_CHECK_EQUAL(allocs[0].storage[0].first,
        "0x0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(allocs[0].storage[0].second,
        "0x0000000000000000000000000000000000000000000000000000000000000001");
}

BOOST_AUTO_TEST_CASE(MissingAddressNamesSection)
{
    // no address key -> boost ptree throws; the wrap must name [alloc.0].
    std::string ini =
        std::string(kBase) + kFeatureL2 + "[alloc.0]\nbalance=0\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](bcos::tool::InvalidConfig const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && comment->find("alloc.0") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(NonHexAddressRejected)
{
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0xZZ000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(BadBalanceNamesSection)
{
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=garbage\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(DuplicateAddressRejected)
{
    // same address, different case -> lowercased dedup must reject.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.1]\naddress=0x42000000000000000000000000000000000000c0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(BadStorageKeyRejected)
{
    // storage key is not 64 hex chars -> reject.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.0.storage]\n0x01=0x01\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}

BOOST_AUTO_TEST_CASE(BadStorageValueRejected)
{
    // storage value is not 64 hex chars -> reject (importGenesisState unhexes it
    // into a 32-byte word; short/odd values would corrupt genesis storage).
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.0.storage]\n"
                      "0x0000000000000000000000000000000000000000000000000000000000000000=0x01\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}


BOOST_AUTO_TEST_CASE(NonceOverflowRejected)
{
    // nonce = 2^64 (one past uint64 max) -> reject; it is decimal-valid but the
    // genesis hash serializes nonce as uint64, so it must not overflow.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x42000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=18446744073709551616\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_THROW(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig);
}
BOOST_AUTO_TEST_SUITE_END()
