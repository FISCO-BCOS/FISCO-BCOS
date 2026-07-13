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
 * @brief: TOML configuration parser for the built-in console
 * @file: ConsoleConfig.h
 */

#pragma once

#include <string>
#include <vector>

namespace bcos::console
{

// Parsed config.toml for the console (mirroring Java SDK config-example.toml)
struct ConsoleConfig
{
    // [cryptoMaterial]
    bool disableSsl = false;
    bool useSMCrypto = false;
    std::string certPath;
    std::string caCert;
    std::string sslCert;
    std::string sslKey;
    std::string smCaCert;
    std::string smSslCert;
    std::string smSslKey;
    std::string smEnSslCert;
    std::string smEnSslKey;

    // [network]
    int64_t messageTimeout = 10000;  // ms
    std::string defaultGroup;
    std::vector<std::string> peers;  // "ip:port" pairs

    // [account]
    std::string keyStoreDir = "account";
    std::string accountFilePath;
    std::string accountFileFormat = "pem";  // "pem" or "p12"
    std::string accountAddress;
    std::string password;

    // [threadPool]
    int threadPoolSize = 0;  // 0 = auto (ncpu)

    // --- resolved paths (filled after loadConfig) ---
    std::string resolvedCaCert;
    std::string resolvedSslCert;
    std::string resolvedSslKey;
    std::string resolvedSmCaCert;
    std::string resolvedSmSslCert;
    std::string resolvedSmSslKey;
    std::string resolvedSmEnSslCert;
    std::string resolvedSmEnSslKey;
};

// Parse config.toml and fill a ConsoleConfig.
// Returns true on success. Errors are printed to stderr.
bool loadConsoleConfig(std::string_view configPath, ConsoleConfig& outConfig);

// Build a quick minimal config for local (in-process) connections.
ConsoleConfig buildLocalConsoleConfig(
    std::string_view groupID, std::string_view nodeName = {});

}  // namespace bcos::console
