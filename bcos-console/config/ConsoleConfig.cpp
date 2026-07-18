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
 * @brief: TOML configuration parser implementation
 * @file: ConsoleConfig.cpp
 */

#include "ConsoleConfig.h"

#include <toml++/toml.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <thread>

namespace fs = boost::filesystem;
using namespace bcos::console;

// Resolve a cert file path relative to certPath or configDir.
static std::string resolveCertPath(
    std::string_view certPath, std::string_view configDir, std::string_view fileName)
{
    if (fileName.empty())
        return {};

    fs::path p(fileName);
    if (p.is_absolute())
        return std::string(fileName);

    if (!certPath.empty())
    {
        auto candidate = fs::path(certPath) / fileName;
        if (fs::exists(candidate))
            return candidate.string();
    }

    auto candidate = fs::path(configDir) / fileName;
    return candidate.string();
}

bool bcos::console::loadConsoleConfig(std::string_view configPath, ConsoleConfig& outConfig)
{
    try
    {
        auto tbl = toml::parse_file(std::string(configPath));

        fs::path configFile(configPath);
        auto configDir = configFile.parent_path().string();

        // ---- [cryptoMaterial] ----
        outConfig.disableSsl =
            tbl["cryptoMaterial"]["disableSsl"].value_or(std::string("false")) == "true";
        outConfig.useSMCrypto =
            tbl["cryptoMaterial"]["useSMCrypto"].value_or(std::string("false")) == "true";
        outConfig.certPath = tbl["cryptoMaterial"]["certPath"].value_or("conf");

        outConfig.caCert = tbl["cryptoMaterial"]["caCert"].value_or("");
        outConfig.sslCert = tbl["cryptoMaterial"]["sslCert"].value_or("");
        outConfig.sslKey = tbl["cryptoMaterial"]["sslKey"].value_or("");

        outConfig.smCaCert = tbl["cryptoMaterial"]["smCaCert"].value_or("");
        outConfig.smSslCert = tbl["cryptoMaterial"]["smSslCert"].value_or("");
        outConfig.smSslKey = tbl["cryptoMaterial"]["smSslKey"].value_or("");
        outConfig.smEnSslCert = tbl["cryptoMaterial"]["smEnSslCert"].value_or("");
        outConfig.smEnSslKey = tbl["cryptoMaterial"]["smEnSslKey"].value_or("");

        // ---- [network] ----
        outConfig.messageTimeout =
            std::stoll(tbl["network"]["messageTimeout"].value_or(
                std::to_string(outConfig.messageTimeout)));
        outConfig.defaultGroup = tbl["network"]["defaultGroup"].value_or("group0");

        if (auto peersArr = tbl["network"]["peers"].as_array())
        {
            outConfig.peers.clear();
            for (size_t i = 0; i < peersArr->size(); ++i)
            {
                auto optVal = (*peersArr)[i].template value<std::string>();
                if (optVal)
                    outConfig.peers.push_back(*optVal);
            }
        }

        // ---- [account] ----
        outConfig.keyStoreDir = tbl["account"]["keyStoreDir"].value_or("account");
        outConfig.accountFilePath = tbl["account"]["accountFilePath"].value_or("");
        outConfig.accountFileFormat = tbl["account"]["accountFileFormat"].value_or("pem");
        outConfig.accountAddress = tbl["account"]["accountAddress"].value_or("");
        outConfig.password = tbl["account"]["password"].value_or("");

        // ---- [threadPool] ----
        auto tpSize = tbl["threadPool"]["threadPoolSize"].value_or(std::string(""));
        outConfig.threadPoolSize = tpSize.empty() ?
                                       static_cast<int>(std::thread::hardware_concurrency()) :
                                       std::stoi(tpSize);

        // ---- resolve cert paths ----
        std::string certPathBase = outConfig.certPath;
        if (certPathBase.empty())
            certPathBase = configDir;
        else if (!fs::path(certPathBase).is_absolute())
            certPathBase = (fs::path(configDir) / certPathBase).string();

        outConfig.resolvedCaCert = resolveCertPath(certPathBase, configDir, outConfig.caCert);
        outConfig.resolvedSslCert = resolveCertPath(certPathBase, configDir, outConfig.sslCert);
        outConfig.resolvedSslKey = resolveCertPath(certPathBase, configDir, outConfig.sslKey);
        outConfig.resolvedSmCaCert = resolveCertPath(certPathBase, configDir, outConfig.smCaCert);
        outConfig.resolvedSmSslCert =
            resolveCertPath(certPathBase, configDir, outConfig.smSslCert);
        outConfig.resolvedSmSslKey =
            resolveCertPath(certPathBase, configDir, outConfig.smSslKey);
        outConfig.resolvedSmEnSslCert =
            resolveCertPath(certPathBase, configDir, outConfig.smEnSslCert);
        outConfig.resolvedSmEnSslKey =
            resolveCertPath(certPathBase, configDir, outConfig.smEnSslKey);

        return true;
    }
    catch (toml::parse_error const& e)
    {
        std::cerr << "Failed to parse config: " << configPath << '\n' << e.what() << '\n';
        return false;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error loading config: " << e.what() << '\n';
        return false;
    }
}

ConsoleConfig bcos::console::buildLocalConsoleConfig(
    std::string_view groupID, std::string_view /*nodeName*/)
{
    ConsoleConfig cfg;
    cfg.defaultGroup = groupID;
    cfg.disableSsl = true;
    cfg.keyStoreDir = "./accounts";
    cfg.accountFileFormat = "pem";
    cfg.threadPoolSize = static_cast<int>(std::thread::hardware_concurrency());
    cfg.peers.clear();
    return cfg;
}

ConsoleConfig bcos::console::buildRemoteConsoleConfig(
    std::string_view peer, std::string_view groupID)
{
    ConsoleConfig cfg;
    cfg.defaultGroup = groupID;
    cfg.disableSsl = true;
    cfg.useSMCrypto = false;
    cfg.keyStoreDir = "./accounts";
    cfg.accountFileFormat = "pem";
    cfg.threadPoolSize = static_cast<int>(std::thread::hardware_concurrency());
    cfg.peers = {std::string(peer)};
    cfg.messageTimeout = 10000;
    return cfg;
}
