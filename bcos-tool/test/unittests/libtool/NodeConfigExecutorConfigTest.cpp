/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
namespace
{
// loadExecutorConfig is protected; expose it to feed hand-built genesis ptrees.
struct ExecutorLoaderProbe : public NodeConfig
{
    using NodeConfig::loadExecutorConfig;
    using NodeConfig::NodeConfig;
};

boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigExecutorConfigTest)

// A default-constructed NodeConfig carries compatibilityVersion == MAX_VERSION
// (>= V3.1 and >= V3.3), so the version-gated executor validations are active.

// wasm requires serial execution: is_wasm=true with is_serial_execute=false is
// rejected at >= V3.1.
BOOST_AUTO_TEST_CASE(wasmWithoutSerialExecuteRejected)
{
    ExecutorLoaderProbe probe;
    BOOST_CHECK_THROW(probe.loadExecutorConfig(fromIni(
                          "[executor]\nis_wasm=true\nis_auth_check=false\nis_serial_execute=false\n"
                          "auth_admin_account=0x1\n")),
        bcos::tool::InvalidConfig);
}

// wasm does not support auth check: is_wasm=true with is_auth_check=true is
// rejected at >= V3.1.
BOOST_AUTO_TEST_CASE(wasmWithAuthCheckRejected)
{
    ExecutorLoaderProbe probe;
    BOOST_CHECK_THROW(probe.loadExecutorConfig(fromIni(
                          "[executor]\nis_wasm=true\nis_auth_check=true\nis_serial_execute=true\n"
                          "auth_admin_account=0x1\n")),
        bcos::tool::InvalidConfig);
}

// At >= V3.3 an empty auth_admin_account is rejected even without auth check.
BOOST_AUTO_TEST_CASE(emptyAuthAdminAccountRejected)
{
    ExecutorLoaderProbe probe;
    BOOST_CHECK_THROW(
        probe.loadExecutorConfig(
            fromIni("[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=true\n")),
        bcos::tool::InvalidConfig);
}

// A solidity, serial-executing config with an admin account is accepted.
BOOST_AUTO_TEST_CASE(validSolidityConfigAccepted)
{
    ExecutorLoaderProbe probe;
    BOOST_CHECK_NO_THROW(probe.loadExecutorConfig(
        fromIni("[executor]\nis_wasm=false\nis_auth_check=true\nis_serial_execute=true\n"
                "auth_admin_account=0x0000000000000000000000000000000000000001\n")));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
