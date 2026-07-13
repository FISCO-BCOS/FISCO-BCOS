/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief: console application implementation
 * @file: ConsoleApp.cpp
 */

#include "ConsoleApp.h"
#include "connection/LocalRpcConnection.h"
#include "connection/RemoteRpcConnection.h"
#include "contract/ContractCompiler.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/sm2/SM2KeyPairFactory.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace bcos::console;

bool ConsoleApp::init(std::string_view configPath, bcos::rpc::JsonRpcInterface::Ptr jsonRpc,
    std::string_view defaultGroup)
{
    m_configPath = configPath;

    if (jsonRpc)
    {
        // Local in-process mode
        m_useLocalRpc = true;
        auto group = defaultGroup.empty() ? "group0" : defaultGroup;
        m_currentGroup = group;
        m_connection = std::make_shared<LocalRpcConnection>(
            std::move(jsonRpc), std::string(group));
        m_consoleConfig = buildLocalConsoleConfig(group);
    }
    else
    {
        // Remote mode — parse config.toml
        if (!loadConsoleConfig(configPath, m_consoleConfig))
        {
            std::cerr << "Failed to load config from: " << configPath << '\n';
            return false;
        }
        m_currentGroup = m_consoleConfig.defaultGroup;
        auto remoteConn = std::make_shared<RemoteRpcConnection>(m_consoleConfig);
        m_connection = remoteConn;
    }

    // Connect
    m_connection->connect();
    if (!m_connection->isConnected())
    {
        std::cerr << "Failed to connect to node(s)\n";
        return false;
    }

    // Initialize key manager — detect crypto type from config
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    ConsoleCryptoType cryptoType;
    bcos::crypto::KeyPairFactory::Ptr keyPairFactory;
    bcos::crypto::Hash::Ptr hashImpl;

    if (m_consoleConfig.useSMCrypto)
    {
        cryptoType = ConsoleCryptoType::SM2;
        keyPairFactory = std::make_shared<bcos::crypto::SM2KeyPairFactory>();
        hashImpl = std::make_shared<bcos::crypto::SM3>();
    }
    else
    {
        cryptoType = ConsoleCryptoType::ECDSA;
        keyPairFactory = std::make_shared<Secp256k1KeyPairFactoryAdapter>();
        hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }
    m_keyManager = std::make_shared<KeyManager>(
        keyFactory, keyPairFactory, hashImpl, cryptoType);

    // Auto-load account from config if specified
    if (!m_consoleConfig.accountFilePath.empty())
    {
        m_keyManager->loadKey(m_consoleConfig.accountFilePath,
            m_consoleConfig.accountFileFormat, m_consoleConfig.password);
    }
    else
    {
        // Try random load from keyStoreDir
        auto accounts = m_keyManager->listAccounts(m_consoleConfig.keyStoreDir);
        if (!accounts.empty())
        {
            m_keyManager->loadPemKey(accounts[0].keyFile);
        }
    }

    // Register commands
    registerCommands();

    return true;
}

std::vector<std::string> ConsoleApp::tokenizeLine(std::string_view line)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char ch = line[i];
        if (inQuotes)
        {
            if (ch == quoteChar)
            {
                inQuotes = false;
            }
            else
            {
                current += ch;
            }
        }
        else if (ch == '"' || ch == '\'')
        {
            inQuotes = true;
            quoteChar = ch;
        }
        else if (ch == ' ' || ch == '\t')
        {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += ch;
        }
    }

    if (!current.empty())
    {
        tokens.push_back(std::move(current));
    }

    return tokens;
}

bool ConsoleApp::processCommand(std::string_view rawLine)
{
    // Skip SQL-like commands that start with keywords (select, insert, etc.)
    // CRUD commands pass the raw line through directly
    auto trimmedLine = rawLine;
    // Trim leading whitespace
    while (!trimmedLine.empty() && (trimmedLine.front() == ' ' || trimmedLine.front() == '\t'))
        trimmedLine.remove_prefix(1);

    if (trimmedLine.empty())
    {
        return true;
    }

    auto tokens = tokenizeLine(trimmedLine);
    if (tokens.empty())
    {
        return true;
    }

    auto& cmdName = tokens[0];

    // Handle CRUD SQL commands — pass raw SQL as a single param
    static const std::vector<std::string> crudCommands = {
        "create", "alter", "insert", "select", "update", "delete", "desc"};
    bool isCrud = std::find(crudCommands.begin(), crudCommands.end(), cmdName) != crudCommands.end();

    // Handle special built-in commands
    if (cmdName == "help" || cmdName == "h" || cmdName == "-h" || cmdName == "--help")
    {
        printHelp();
        return true;
    }

    if (cmdName == "quit" || cmdName == "q" || cmdName == "exit")
    {
        return false;  // signal exit
    }

    if (cmdName == "switch" || cmdName == "s")
    {
        if (tokens.size() < 2)
        {
            std::cout << "Usage: switch <groupID>\n";
            return true;
        }
        m_currentGroup = tokens[1];
        std::cout << "Switched to group: " << m_currentGroup << '\n';
        return true;
    }

    if (cmdName == "setNodeName")
    {
        if (tokens.size() < 2)
        {
            std::cout << "Usage: setNodeName <nodeName>\n";
            return true;
        }
        m_connection->setDefaultNodeName(tokens[1]);
        std::cout << "Default node set to: " << tokens[1] << '\n';
        return true;
    }

    if (cmdName == "getNodeName")
    {
        auto name = m_connection->defaultNodeName();
        if (name.empty())
        {
            std::cout << "No default node name (random routing)\n";
        }
        else
        {
            std::cout << "Default node: " << name << '\n';
        }
        return true;
    }

    if (cmdName == "clearNodeName")
    {
        m_connection->clearDefaultNodeName();
        std::cout << "Default node name cleared (random routing enabled)\n";
        return true;
    }

    // Lookup command in dispatcher
    auto cmdInfo = m_dispatcher.findCommand(cmdName);
    if (!cmdInfo)
    {
        std::cout << "Unknown command: " << cmdName
                  << ". Type 'help' for available commands.\n";
        return true;
    }

    // Prepare params (exclude command name)
    std::vector<std::string> params(tokens.begin() + 1, tokens.end());

    if (isCrud)
    {
        // For CRUD commands, pass the raw SQL as a single string
        params = {std::string(trimmedLine.substr(cmdName.size()))};
        // Trim leading space
        if (!params[0].empty() && params[0][0] == ' ')
            params[0] = params[0].substr(1);
    }

    // Validate param count
    if (cmdInfo->maxParams >= 0)
    {
        if ((int)params.size() < cmdInfo->minParams)
        {
            std::cout << "Too few parameters. Usage: " << cmdInfo->help << '\n';
            return true;
        }
        if ((int)params.size() > cmdInfo->maxParams)
        {
            std::cout << "Too many parameters. Usage: " << cmdInfo->help << '\n';
            return true;
        }
    }

    // Execute
    try
    {
        bool ok = cmdInfo->callback(params, m_currentPwd);
        if (!ok)
        {
            std::cout << "Command failed.\n";
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "Error: " << e.what() << '\n';
    }

    return true;
}

void ConsoleApp::printHelp()
{
    OutputFormatter::printDoubleLine();
    std::cout << "FISCO BCOS Console — Available Commands:\n";
    OutputFormatter::printDoubleLine();

    for (auto const& category : m_dispatcher.categories())
    {
        std::cout << "\n[" << category.name << "]\n";
        OutputFormatter::printSingleLine();
        for (auto const& cmd : category.commands)
        {
            std::cout << "  " << cmd.name;
            if (!cmd.aliases.empty())
            {
                std::cout << " (";
                for (size_t i = 0; i < cmd.aliases.size(); ++i)
                {
                    if (i > 0)
                        std::cout << ", ";
                    std::cout << cmd.aliases[i];
                }
                std::cout << ")";
            }
            std::cout << "\n    " << cmd.help << "\n";
        }
    }
    OutputFormatter::printBlankLine();
}

std::vector<std::string> ConsoleApp::onCompletion(std::string_view prefix)
{
    return m_dispatcher.completions(prefix);
}

void ConsoleApp::registerCommands()
{
    // This is a placeholder — actual command registration happens in Phase 4.
    // For now, register a minimal set to demonstrate the framework.

    CommandCategory basicCat;
    basicCat.name = "Basic";

    basicCat.commands.push_back({"help", {"h", "-h", "--help"}, 0, 0, false, false, true,
        "help: Show this help message",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            printHelp();
            return true;
        }});

    basicCat.commands.push_back({"quit", {"q", "exit"}, 0, 0, false, false, true,
        "quit: Exit the console",
        [](std::vector<std::string> const&, std::string&) -> bool { return false; }});

    basicCat.commands.push_back({"switch", {"s"}, 1, 1, false, false, true,
        "switch <groupID>: Switch to another group",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            m_currentGroup = params[0];
            std::cout << "Switched to group: " << m_currentGroup << '\n';
            return true;
        }});

    basicCat.commands.push_back({"setNodeName", {}, 1, 1, false, false, true,
        "setNodeName <nodeName>: Set the default node for requests",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            m_connection->setDefaultNodeName(params[0]);
            std::cout << "Default node set to: " << params[0] << '\n';
            return true;
        }});

    basicCat.commands.push_back({"getNodeName", {}, 0, 0, false, false, true,
        "getNodeName: Get the current default node",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            auto name = m_connection->defaultNodeName();
            std::cout << (name.empty() ? "(random routing)" : name) << '\n';
            return true;
        }});

    basicCat.commands.push_back({"clearNodeName", {}, 0, 0, false, false, true,
        "clearNodeName: Clear the default node (random routing)",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->clearDefaultNodeName();
            std::cout << "Default node cleared.\n";
            return true;
        }});

    m_dispatcher.addCategory(basicCat);
    for (auto& cmd : basicCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // Status Query commands — minimal subset
    CommandCategory statusCat;
    statusCat.name = "Status Query";

    statusCat.commands.push_back({"getBlockNumber", {"getblocknumber"}, 0, 0, true, false, true,
        "getBlockNumber: Query the latest block number",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getBlockNumber(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    std::cout << result.asInt64() << '\n';
                });
            return true;
        }});

    statusCat.commands.push_back({"getPbftView", {"getpbftview"}, 0, 0, true, false, true,
        "getPbftView: Query the PBFT view",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getPbftView(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    std::cout << result.asInt64() << '\n';
                });
            return true;
        }});

    statusCat.commands.push_back({"getPeers", {"getpeers"}, 0, 0, false, false, true,
        "getPeers: Query connected peers",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getPeers(
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getGroupList", {"getgrouplist"}, 0, 0, false, false, true,
        "getGroupList: List all group IDs",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getGroupList(
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getSyncStatus", {"getsyncstatus"}, 0, 0, true, false, true,
        "getSyncStatus: Query sync status",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getSyncStatus(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getConsensusStatus", {"getconsensusstatus"}, 0, 0, true, false,
        true, "getConsensusStatus: Query consensus status",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getConsensusStatus(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getSealerList", {"getsealerlist"}, 0, 0, true, false, true,
        "getSealerList: Query sealer list",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getSealerList(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getObserverList", {"getobserverlist"}, 0, 0, true, false, true,
        "getObserverList: Query observer list",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getObserverList(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back({"getPendingTxSize", {"getpendingtxsize"}, 0, 0, true, false, true,
        "getPendingTxSize: Query pending transaction count",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getPendingTxSize(m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    std::cout << result.asInt64() << '\n';
                });
            return true;
        }});

    statusCat.commands.push_back({"getTotalTransactionCount", {"gettotaltxcount"}, 0, 0, true,
        false, true, "getTotalTransactionCount: Query total transaction count",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getTotalTransactionCount(
                m_currentGroup, m_connection->defaultNodeName(),
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    statusCat.commands.push_back(
        {"getSystemConfigByKey", {"getsystemconfigbykey"}, 1, 1, true, false, true,
            "getSystemConfigByKey <key>: Query system config value",
            [this](std::vector<std::string> const& params, std::string&) -> bool {
                m_connection->getSystemConfigByKey(
                    m_currentGroup, m_connection->defaultNodeName(), params[0],
                    [](bcos::Error::Ptr error, Json::Value& result) {
                        if (error)
                        {
                            std::cout << "Error: " << error->errorMessage() << '\n';
                            return;
                        }
                        OutputFormatter::printJson(result);
                    });
                return true;
            }});

    m_dispatcher.addCategory(statusCat);
    for (auto& cmd : statusCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // Block Query commands
    CommandCategory blockCat;
    blockCat.name = "Block Query";

    blockCat.commands.push_back({"getBlockByNumber", {"getblockbynumber"}, 1, 2, true, false, true,
        "getBlockByNumber <blockNumber> [onlyHash]: Query block by number",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            int64_t number = std::stoll(params[0]);
            bool onlyTxHash = params.size() > 1 && params[1] == "true";
            m_connection->getBlockByNumber(m_currentGroup, m_connection->defaultNodeName(), number,
                false, onlyTxHash, [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    blockCat.commands.push_back({"getBlockByHash", {"getblockbyhash"}, 1, 2, true, false, true,
        "getBlockByHash <blockHash> [onlyHash]: Query block by hash",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            bool onlyTxHash = params.size() > 1 && params[1] == "true";
            m_connection->getBlockByHash(m_currentGroup, m_connection->defaultNodeName(), params[0],
                false, onlyTxHash, [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    blockCat.commands.push_back({"getBlockHashByNumber", {"getblockhashbynumber"}, 1, 1, true,
        false, true, "getBlockHashByNumber <blockNumber>: Query block hash by number",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            int64_t number = std::stoll(params[0]);
            m_connection->getBlockHashByNumber(m_currentGroup, m_connection->defaultNodeName(),
                number, [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    std::cout << result.asString() << '\n';
                });
            return true;
        }});

    blockCat.commands.push_back({"getTransactionByHash", {"gettransactionbyhash"}, 1, 1, true,
        false, true, "getTransactionByHash <txHash>: Query transaction by hash",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            m_connection->getTransaction(m_currentGroup, m_connection->defaultNodeName(), params[0],
                false, [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    blockCat.commands.push_back({"getTransactionReceipt", {"gettransactionreceipt"}, 1, 1, true,
        false, true, "getTransactionReceipt <txHash>: Query transaction receipt by hash",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            m_connection->getTransactionReceipt(m_currentGroup, m_connection->defaultNodeName(),
                params[0], false, [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    blockCat.commands.push_back({"getCode", {"getcode"}, 1, 1, true, false, true,
        "getCode <contractAddress>: Query contract bytecode",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            m_connection->getCode(m_currentGroup, m_connection->defaultNodeName(), params[0],
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    std::cout << result.asString() << '\n';
                });
            return true;
        }});

    m_dispatcher.addCategory(blockCat);
    for (auto& cmd : blockCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // Group Query commands
    CommandCategory groupCat;
    groupCat.name = "Group Query";

    groupCat.commands.push_back({"getGroupInfo", {"getgroupinfo"}, 0, 0, true, false, true,
        "getGroupInfo: Get current group information",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getGroupInfo(m_currentGroup,
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    groupCat.commands.push_back({"getGroupInfoList", {"getgroupinfolist"}, 0, 0, false, false, true,
        "getGroupInfoList: List all group information",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getGroupInfoList(
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    groupCat.commands.push_back({"getGroupPeers", {"getgrouppeers"}, 0, 0, true, false, true,
        "getGroupPeers: List peers in current group",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            m_connection->getGroupPeers(m_currentGroup,
                [](bcos::Error::Ptr error, Json::Value& result) {
                    if (error)
                    {
                        std::cout << "Error: " << error->errorMessage() << '\n';
                        return;
                    }
                    OutputFormatter::printJson(result);
                });
            return true;
        }});

    m_dispatcher.addCategory(groupCat);
    for (auto& cmd : groupCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // ---- Account commands ----
    CommandCategory accountCat;
    accountCat.name = "Account";

    accountCat.commands.push_back({"newAccount", {}, 0, 2, false, false, true,
        "newAccount [accountFormat] [password]: Create a new local account (pem/p12)",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            auto format = params.size() >= 1 ? params[0] : "pem";
            if (format == "p12")
            {
                return m_keyManager->newP12Key(m_consoleConfig.keyStoreDir,
                    params.size() >= 2 ? params[1] : "");
            }
            return m_keyManager->newPemKey(m_consoleConfig.keyStoreDir);
        }});

    accountCat.commands.push_back({"loadAccount", {}, 1, 2, false, false, true,
        "loadAccount <accountPath> [accountFormat]: Load an account key file",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            auto format = params.size() >= 2 ? params[1] : "pem";
            return m_keyManager->loadKey(params[0], format);
        }});

    accountCat.commands.push_back({"listAccount", {}, 0, 0, false, false, true,
        "listAccount: List all local accounts",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            auto accounts = m_keyManager->listAccounts(m_consoleConfig.keyStoreDir);
            if (accounts.empty())
            {
                std::cout << "No accounts found in: " << m_consoleConfig.keyStoreDir << '\n';
                return true;
            }
            std::vector<std::string> headers = {"Address", "Format", "Current", "Path"};
            std::vector<std::vector<std::string>> rows;
            for (auto const& acc : accounts)
            {
                rows.push_back(
                    {acc.address, acc.format, acc.isCurrent ? "*" : "", acc.keyFile});
            }
            OutputFormatter::printTable(headers, rows);
            return true;
        }});

    accountCat.commands.push_back({"getCurrentAccount", {"currentaccount"}, 0, 0, false, false, true,
        "getCurrentAccount: Show the currently loaded account address",
        [this](std::vector<std::string> const&, std::string&) -> bool {
            auto addr = m_keyManager->currentAddress();
            if (addr.empty())
            {
                std::cout << "No account loaded.\n";
            }
            else
            {
                std::cout << "Current account: 0x" << addr << '\n';
            }
            return true;
        }});

    m_dispatcher.addCategory(accountCat);
    for (auto& cmd : accountCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // ---- Consensus Governance commands ----
    CommandCategory consensusCat;
    consensusCat.name = "Consensus Governance";

    consensusCat.commands.push_back({"addSealer", {}, 2, 2, true, true, true,
        "addSealer <nodeID> <weight>: Add a consensus sealer node",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "addSealer: This command requires sending a governance transaction.\n"
                      << "Full implementation will use precompiled contract call.\n"
                      << "Params: nodeID=" << params[0] << " weight=" << params[1] << '\n';
            return true;
        }});

    consensusCat.commands.push_back({"addObserver", {}, 1, 1, true, true, true,
        "addObserver <nodeID>: Add an observer node",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "addObserver: nodeID=" << params[0] << " (precompiled impl pending)\n";
            return true;
        }});

    consensusCat.commands.push_back({"removeNode", {}, 1, 1, true, true, true,
        "removeNode <nodeID>: Remove a node from the group",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "removeNode: nodeID=" << params[0] << " (precompiled impl pending)\n";
            return true;
        }});

    m_dispatcher.addCategory(consensusCat);
    for (auto& cmd : consensusCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // ---- Contract Operation commands ----
    CommandCategory contractCat;
    contractCat.name = "Contract Operation";

    contractCat.commands.push_back({"deploy", {}, 1, -1, true, false, true,
        "deploy <contractName/Path> [params...]: Compile and deploy a Solidity contract",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            auto result = ContractCompiler::compile(params[0]);
            if (!result.success)
            {
                std::cerr << "Compilation failed:\n" << result.errorMessage << '\n';
                return false;
            }
            std::cout << "Compiled: " << result.contractName << '\n';
            // Cache ABI + BIN
            ContractCompiler::saveToCache(result.contractName, result.abi, result.bin);
            // sendTransaction to deploy...
            std::cout << "Deploy: sendTransaction with bin.length=" << result.bin.size()
                      << " (full deploy needs transaction signing integration)\n";
            return true;
        }});

    contractCat.commands.push_back({"call", {}, 3, -1, true, false, true,
        "call <contractName> <address> <func> [params...]: Call a contract function",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            auto cached = ContractCompiler::loadFromCache(params[0]);
            if (!cached.found)
            {
                std::cerr << "Contract not found in cache: " << params[0]
                          << ". Deploy it first.\n";
                return false;
            }
            std::cout << "call " << params[0] << " at " << params[1] << " func=" << params[2]
                      << " (ABI encoding via bcos-codec pending)\n";
            return true;
        }});

    contractCat.commands.push_back({"getDeployLog", {}, 0, 1, false, false, true,
        "getDeployLog [recordNumber]: Query last deployment records (default 20)",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            int limit = 20;
            if (!params.empty()) limit = std::stoi(params[0]);
            auto cached = ContractCompiler::listCached();
            std::vector<std::string> headers = {"Contract", "Address"};
            std::vector<std::vector<std::string>> rows;
            for (auto const& name : cached)
            {
                auto addrs = ContractCompiler::getDeployAddresses(name, limit);
                for (auto const& addr : addrs)
                    rows.push_back({name, addr});
            }
            if (rows.empty())
                std::cout << "No deployment records.\n";
            else
                OutputFormatter::printTable(headers, rows);
            return true;
        }});

    contractCat.commands.push_back({"listDeployContractAddress", {}, 1, 2, false, false, true,
        "listDeployContractAddress <contractName> [recordNumber]: List deployed addresses",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            int limit = params.size() >= 2 ? std::stoi(params[1]) : 20;
            auto addrs = ContractCompiler::getDeployAddresses(params[0], limit);
            if (addrs.empty())
                std::cout << "No deployed addresses for: " << params[0] << '\n';
            else
                for (size_t i = 0; i < addrs.size(); ++i)
                    std::cout << "[" << i << "] " << addrs[i] << '\n';
            return true;
        }});

    contractCat.commands.push_back({"transfer", {}, 2, 3, true, false, true,
        "transfer <toAddress> <amount> [unit]: Transfer balance to an address",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "transfer " << params[1] << " to " << params[0]
                      << " (transaction signing integration pending)\n";
            return true;
        }});

    m_dispatcher.addCategory(contractCat);
    for (auto& cmd : contractCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }

    // ---- BFS commands ----
    CommandCategory bfsCat;
    bfsCat.name = "BFS (Blockchain File System)";

    bfsCat.commands.push_back({"pwd", {}, 0, 0, true, false, true,
        "pwd: Print current working directory",
        [this](std::vector<std::string> const&, std::string& pwd) -> bool {
            std::cout << pwd << '\n';
            return true;
        }});

    bfsCat.commands.push_back({"cd", {}, 1, 1, true, false, true,
        "cd <path>: Change working directory",
        [this](std::vector<std::string> const& params, std::string& pwd) -> bool {
            auto newPath = params[0];
            if (newPath.starts_with("/"))
                m_currentPwd = newPath;
            else if (newPath.starts_with("~"))
                m_currentPwd = "/apps/" + newPath.substr(1);
            else
                m_currentPwd = pwd + "/" + newPath;
            // Normalize: remove trailing slashes
            while (m_currentPwd.size() > 1 && m_currentPwd.back() == '/')
                m_currentPwd.pop_back();
            std::cout << "Current dir: " << m_currentPwd << '\n';
            return true;
        }});

    bfsCat.commands.push_back({"ls", {}, 0, 1, true, false, true,
        "ls [path]: List BFS directory contents",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "ls (BFS list via precompiled 0x100e — pending)\n";
            return true;
        }});

    bfsCat.commands.push_back({"mkdir", {}, 1, 1, true, false, true,
        "mkdir <path>: Create a BFS directory",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "mkdir " << params[0] << " (BFS mkdir via precompiled — pending)\n";
            return true;
        }});

    bfsCat.commands.push_back({"tree", {}, 0, 2, true, false, true,
        "tree [path] [limit]: Tree view of BFS directory",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "tree (BFS tree via precompiled — pending)\n";
            return true;
        }});

    bfsCat.commands.push_back({"ln", {}, 2, 2, true, false, true,
        "ln <path> <contractAddress>: Create a BFS link to a contract",
        [this](std::vector<std::string> const& params, std::string&) -> bool {
            std::cout << "ln " << params[0] << " -> " << params[1]
                      << " (BFS link via precompiled — pending)\n";
            return true;
        }});

    m_dispatcher.addCategory(bfsCat);
    for (auto& cmd : bfsCat.commands)
    {
        m_dispatcher.addCommand(std::move(cmd));
    }
}

void ConsoleApp::start()
{
    m_running = true;

    OutputFormatter::printWelcome("3.9.1");
    std::cout << "Connected to group: " << m_currentGroup << '\n';
    if (!m_promptPrefix.empty())
        std::cout << m_promptPrefix << '\n';

    // Setup REPL with completion
    m_repl.setCompleter([this](std::string_view prefix) {
        return m_dispatcher.completions(prefix);
    });
    m_repl.setHistoryFile(".fisco-bcos-console-history");
    m_repl.loadHistory();

    bool isTTY = isatty(STDIN_FILENO);

    while (m_running)
    {
        if (isTTY)
        {
            auto prompt = OutputFormatter::buildPrompt(m_currentGroup, m_currentPwd);
            m_repl.setPrompt(prompt);
        }

        auto line = m_repl.readLine();
        if (line.empty())
            break;  // EOF (pipe) or Ctrl-D (TTY)

        m_running = processCommand(line);
    }

    m_repl.saveHistory();
    std::cout << "Goodbye.\n";
}

void ConsoleApp::stop()
{
    m_running = false;
    if (m_connection)
    {
        m_connection->disconnect();
    }
}
