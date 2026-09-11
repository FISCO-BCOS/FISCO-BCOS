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

/// Default message timeout in milliseconds (10 seconds).
inline constexpr int64_t CONSOLE_DEFAULT_MSG_TIMEOUT_MS = 10000;

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
    int64_t messageTimeout = CONSOLE_DEFAULT_MSG_TIMEOUT_MS;  // ms
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

// Build a minimal config for direct remote connection (no config.toml).
// peer is "ip:port", groupID is the default group.
ConsoleConfig buildRemoteConsoleConfig(std::string_view peer, std::string_view groupID = "group0");

}  // namespace bcos::console
