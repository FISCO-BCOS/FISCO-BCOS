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
 * @brief: Solidity compiler integration via solc subprocess
 * @file: ContractCompiler.h
 */

#pragma once

#include <json/json.h>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::console
{

// Result of a solc compilation.
struct CompilationResult
{
    bool success = false;
    std::string contractName;  // e.g. "HelloWorld.sol:HelloWorld"
    std::string abi;           // JSON ABI string
    std::string bin;           // hex bytecode (without 0x prefix)
    std::string smBin;         // SM crypto bytecode (without 0x prefix), may be empty
    std::string errorMessage;  // filled on failure
};

// Result of looking up a cached contract.
struct CachedContract
{
    bool found = false;
    std::string abi;     // JSON ABI string
    std::string bin;     // hex bytecode (without 0x prefix)
    std::string address; // deployed address (if known), hex without 0x
};

// Compile a Solidity contract by calling the system solc binary.
// Falls back to cached .abi/.bin files if available.
class ContractCompiler
{
public:
    // Compile a .sol file and return ABI + BIN.
    // sourcePath: path to .sol file (absolute or relative to contracts/solidity/)
    // sm: if true, also include SM compilation output
    // optimize: enable solc optimizer
    static CompilationResult compile(
        std::string_view sourcePath, bool sm = false, bool optimize = true);

    // ---- Cache management ----
    // Save compiled ABI and BIN to contracts/.compiled/
    static bool saveToCache(
        std::string_view contractName, std::string_view abi, std::string_view bin);

    // Load cached contract by name.
    static CachedContract loadFromCache(std::string_view contractName);

    // List all cached contracts.
    static std::vector<std::string> listCached();

    // Record a deployed contract address (for listDeployContractAddress).
    static bool saveDeployAddress(std::string_view contractName, std::string_view address);

    // Get the last N deploy addresses for a contract.
    static std::vector<std::string> getDeployAddresses(
        std::string_view contractName, int limit = 20);

private:
    // Path constants matching Java console layout.
    static constexpr auto SOLIDITY_DIR = "contracts/solidity/";
    static constexpr auto COMPILED_DIR = "contracts/.compiled/";

    // Resolve a contract name/path to an absolute file path.
    static std::string resolvePath(std::string_view nameOrPath);

    // Parse solc --combined-json output.
    static CompilationResult parseSolcOutput(
        std::string_view jsonOutput, std::string_view sourcePath);
};

}  // namespace bcos::console
