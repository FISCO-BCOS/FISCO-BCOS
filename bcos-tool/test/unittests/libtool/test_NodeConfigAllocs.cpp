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
#include "ExceptionCheck.h"
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
    "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
    "balance=0\nnonce=0\ncode=0x6080604052\n";

// A valid [eth_genesis_header] section: L2 mode now REQUIRES it
// (validateL2Invariants binds the section and the feature both ways).
constexpr auto kAllocsEthHeader =
    "[eth_genesis_header]\n"
    "parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
    "sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347\n"
    "miner=0x4200000000000000000000000000000000000011\n"
    "state_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
    "transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
    "receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
    "logs_bloom="
    "0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00"
    "000000000000000000000000000000000000\n"
    "difficulty=0x0\nnumber=0x0\ngas_limit=0x1c9c380\ngas_used=0x0\n"
    "timestamp=0x689d5c00\nextra_data=0x00000000fa000000060000000000000000\n"
    "mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
    "nonce=0x0000000000000000\nbase_fee_per_gas=0x3b9aca00\n"
    "withdrawals_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
    "blob_gas_used=0x0\nexcess_blob_gas=0x0\n"
    "parent_beacon_block_root=0x0000000000000000000000000000000000000000000000000000000000000000\n"
    "requests_hash=0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
    "hash=0xb153f41d2651441ace825becbfe2f2b6bf89092864a0ae04b7e0d40a5cf64cc1\n";
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
    cfg->loadGenesisConfig(parseIni(std::string(kBase) + kFeatureL2 + kAlloc0 + kAllocsEthHeader));
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs.size(), 1U);
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs[0].address,
        "0x43000000000000000000000000000000000000c0");  // forced lowercase
    BOOST_CHECK_EQUAL(cfg->genesisConfig().m_allocs[0].code, "0x6080604052");
}

BOOST_AUTO_TEST_CASE(FeatureL2RejectsEmptyAllocs)
{
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(std::string(kBase) + kFeatureL2)),
        bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, "requires a non-empty [alloc.*] section");
        });
}

BOOST_AUTO_TEST_CASE(AllocsWithoutFeatureRejected)
{
    // allocs present but feature_l2_ethereum_compat not enabled -> reject
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(
        cfg->loadGenesisConfig(parseIni(std::string(kBase) + kAlloc0)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, "[alloc.*] section requires");
        });
}

BOOST_AUTO_TEST_CASE(AllocStorageSlotsParsed)
{
    std::string ini = std::string(kBase) + kFeatureL2 + kAllocsEthHeader +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
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
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, ".address is not valid hex");
        });
}

BOOST_AUTO_TEST_CASE(BadBalanceNamesSection)
{
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
                      "balance=garbage\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, ".balance must be decimal digits");
        });
}

BOOST_AUTO_TEST_CASE(DuplicateAddressRejected)
{
    // same address, different case -> lowercased dedup must reject.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.1]\naddress=0x43000000000000000000000000000000000000c0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, "address duplicate");
        });
}

BOOST_AUTO_TEST_CASE(BadStorageKeyRejected)
{
    // storage key is not 64 hex chars -> reject.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.0.storage]\n0x01=0x01\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, ".storage].key must be 64 hex chars");
        });
}

BOOST_AUTO_TEST_CASE(BadStorageValueRejected)
{
    // storage value is not 64 hex chars -> reject (importGenesisState unhexes it
    // into a 32-byte word; short/odd values would corrupt genesis storage).
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=0\ncode=0x6080604052\n"
                      "[alloc.0.storage]\n"
                      "0x0000000000000000000000000000000000000000000000000000000000000000=0x01\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, ".storage].value must be 64 hex chars");
        });
}


BOOST_AUTO_TEST_CASE(NonceOverflowRejected)
{
    // nonce = 2^64 (one past uint64 max) -> reject; it is decimal-valid but the
    // genesis hash serializes nonce as uint64, so it must not overflow.
    std::string ini = std::string(kBase) + kFeatureL2 +
                      "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
                      "balance=0\nnonce=18446744073709551616\ncode=0x6080604052\n";
    auto cfg = makeNodeConfig();
    BOOST_CHECK_EXCEPTION(cfg->loadGenesisConfig(parseIni(ini)), bcos::tool::InvalidConfig,
        [](auto const& e) {
            return bcos::test::errinfoContains(e, ".nonce must fit in uint64");
        });
}
BOOST_AUTO_TEST_SUITE_END()
