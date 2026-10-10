/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/Cli.h>
#include <bcos-ops/NodeDir.h>
#include <bcos-ops/OpsError.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
struct TempNodeDir
{
    std::filesystem::path dir;
    TempNodeDir()
    {
        dir = std::filesystem::temp_directory_path() /
              ("bcos_ops_nodedir_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
    }
    ~TempNodeDir() { std::filesystem::remove_all(dir); }
    void write(std::string const& _name, std::string const& _content)
    {
        std::ofstream out(dir / _name);
        out << _content;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(CliDispatchTest)

BOOST_AUTO_TEST_CASE(unknownSubcommandExitsOne)
{
    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps({"fisco-bcos", "nope"}, out, err);
    BOOST_CHECK_EQUAL(code, c_exitUsage);
    BOOST_CHECK(err.str().find("unknown subcommand") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(statusHelpExitsZero)
{
    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps({"fisco-bcos", "status", "--help"}, out, err);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK(out.str().find("fisco-bcos status") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(noSubcommandPrintsUsage)
{
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos"}, out, err), c_exitUsage);
    BOOST_CHECK(err.str().find("subcommands:") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(nodeDirReadsIniAndGenesis)
{
    TempNodeDir temp;
    temp.write("config.ini",
        "[rpc]\n"
        "    listen_ip=0.0.0.0\n"
        "    listen_port=20200\n"
        "    disable_ssl=true\n"
        "[storage]\n"
        "    data_path=data\n"
        "[txpool]\n"
        "    limit=15000\n"
        "[log]\n"
        "    log_path=log\n");
    temp.write("config.genesis",
        "[chain]\n"
        "    sm_crypto=false\n"
        "    group_id=group0\n"
        "    chain_id=chain0\n"
        "[executor]\n"
        "    is_auth_check=true\n"
        "[consensus]\n"
        "    consensus_timeout=3000\n");
    auto node = NodeDir::load(temp.dir.string());
    BOOST_REQUIRE(node.consensusTimeoutMs);
    BOOST_CHECK_EQUAL(*node.consensusTimeoutMs, 3000);
    BOOST_CHECK_EQUAL(node.rpcEndpoint(), "127.0.0.1:20200");
    BOOST_CHECK(!node.tls());
    BOOST_CHECK_EQUAL(node.groupId, "group0");
    BOOST_CHECK_EQUAL(node.chainId, "chain0");
    BOOST_CHECK(!node.smCrypto);
    BOOST_CHECK(node.authCheck);
    BOOST_CHECK_EQUAL(node.txpoolLimit, 15000U);
    BOOST_CHECK_EQUAL(node.ipcPath(), (temp.dir / "data" / "fisco-bcos.ipc").string());
    BOOST_CHECK_EQUAL(node.logDir(), (temp.dir / "log").string());
}

BOOST_AUTO_TEST_CASE(nodeDirDefaultsWhenKeysAbsent)
{
    TempNodeDir temp;
    temp.write("config.ini", "[p2p]\n    listen_port=30300\n");
    temp.write("config.genesis", "[chain]\n    group_id=group0\n");
    auto node = NodeDir::load(temp.dir.string());
    BOOST_CHECK_EQUAL(node.rpcEndpoint(), "127.0.0.1:20200");
    BOOST_CHECK(node.tls());  // disable_ssl defaults to false, i.e. SSL on
    BOOST_CHECK_EQUAL(node.storagePath, "data/group0");
}

BOOST_AUTO_TEST_CASE(nodeDirEnableSslOverridesDisableSsl)
{
    TempNodeDir temp;
    temp.write("config.ini", "[rpc]\n    disable_ssl=true\n    enable_ssl=true\n");
    temp.write("config.genesis", "[chain]\n    group_id=group0\n");
    BOOST_CHECK(NodeDir::load(temp.dir.string()).tls());
    temp.write("config.ini", "[rpc]\n    enable_ssl=false\n");
    BOOST_CHECK(!NodeDir::load(temp.dir.string()).tls());
}

BOOST_AUTO_TEST_CASE(nodeDirMissingIniThrowsUsage)
{
    TempNodeDir temp;
    BOOST_CHECK_EXCEPTION(NodeDir::load(temp.dir.string()), OpsError,
        [](OpsError const& e) { return e.exitCode == c_exitUsage; });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
