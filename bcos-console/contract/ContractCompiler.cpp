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
 * @brief: Solidity compiler implementation via solc subprocess
 * @file: ContractCompiler.cpp
 */

#include "ContractCompiler.h"
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace fs = boost::filesystem;
using namespace bcos::console;

// Default solc binary path — can be overridden at compile time via -DSOLC_PATH
#ifndef SOLC_PATH
#define SOLC_PATH "solc"
#endif

// ---- Internal: run a command and capture stdout ----

static std::string runCommand(std::string const& cmd)
{
    std::array<char, 4096> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        throw std::runtime_error("popen() failed for: " + cmd);
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        result += buffer.data();
    }
    int ret = pclose(pipe);
    if (ret != 0 && result.empty())
    {
        throw std::runtime_error("Command failed (exit " + std::to_string(ret) + "): " + cmd);
    }
    return result;
}

// ---- Path resolution ----

std::string ContractCompiler::resolvePath(std::string_view nameOrPath)
{
    fs::path p(nameOrPath);

    // 1. Absolute path with .sol
    if (fs::exists(p) && !fs::is_directory(p))
        return fs::absolute(p).string();

    // 2. Append .sol and retry
    auto withSol = p.string() + ".sol";
    if (fs::exists(withSol))
        return fs::absolute(withSol).string();

    // 3. Try contracts/solidity/ directory
    auto stem = p.stem().string();  // remove .sol if present
    fs::path relPath = fs::path(SOLIDITY_DIR) / (stem + ".sol");
    if (fs::exists(relPath))
        return fs::absolute(relPath).string();

    // 4. As-is under contracts/solidity
    relPath = fs::path(SOLIDITY_DIR) / p.filename();
    if (fs::exists(relPath))
        return fs::absolute(relPath).string();

    return {};  // not found
}

// ---- Compilation ----

CompilationResult ContractCompiler::compile(
    std::string_view sourcePath, bool sm, bool optimize)
{
    CompilationResult result;

    auto resolved = resolvePath(sourcePath);
    if (resolved.empty())
    {
        // Try cache lookup
        auto cached = loadFromCache(sourcePath);
        if (cached.found)
        {
            result.success = true;
            result.contractName = sourcePath;
            result.abi = cached.abi;
            result.bin = cached.bin;
            return result;
        }
        result.errorMessage = "Contract not found: " + std::string(sourcePath) +
                              ". Check contracts/solidity/ directory.";
        return result;
    }

    // Build solc command
    std::ostringstream cmd;
    cmd << SOLC_PATH " --combined-json abi,bin";
    if (optimize)
        cmd << " --optimize";
    if (!sm)
        cmd << " --no-cbor-metadata";  // reproducible builds

    // Add base-path for import resolution
    auto parentDir = fs::path(resolved).parent_path().string();
    cmd << " --allow-paths " << parentDir << " .";
    cmd << " " << resolved;
    cmd << " 2>&1";  // capture stderr too

    std::string output;
    try
    {
        output = runCommand(cmd.str());
    }
    catch (std::exception const& e)
    {
        result.errorMessage = std::string("solc execution failed: ") + e.what() +
                              "\nIs solc installed? Try: apt install solc";
        return result;
    }

    return parseSolcOutput(output, resolved);
}

CompilationResult ContractCompiler::parseSolcOutput(
    std::string_view jsonOutput, std::string_view sourcePath)
{
    CompilationResult result;

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(jsonOutput.begin(), jsonOutput.end(), root))
    {
        // solc might output errors as plain text
        result.errorMessage = "solc parse error:\n" + std::string(jsonOutput);
        return result;
    }

    // Check for compilation errors
    if (root.isMember("errors"))
    {
        bool hasError = false;
        std::ostringstream errors;
        for (auto const& err : root["errors"])
        {
            auto severity = err["severity"].asString();
            auto msg = err["formattedMessage"].asString();
            if (severity == "error")
            {
                hasError = true;
                errors << msg << '\n';
            }
        }
        if (hasError)
        {
            result.errorMessage = errors.str();
            return result;
        }
    }

    // Extract contracts from --combined-json output
    if (!root.isMember("contracts"))
    {
        result.errorMessage = "No contracts in solc output";
        return result;
    }

    auto contracts = root["contracts"];
    auto contractNames = contracts.getMemberNames();
    if (contractNames.empty())
    {
        result.errorMessage = "No contracts compiled";
        return result;
    }

    // Take the first contract (or match by base name)
    std::string selectedName = contractNames[0];
    auto baseName = fs::path(sourcePath).stem().string();
    for (auto const& name : contractNames)
    {
        if (name.find(baseName) != std::string::npos)
        {
            selectedName = name;
            break;
        }
    }

    auto const& contractData = contracts[selectedName];
    result.contractName = selectedName;
    result.success = true;
    result.abi = contractData["abi"].asString();
    result.bin = contractData["bin"].asString();

    // Strip 0x prefix from bin
    if (result.bin.starts_with("0x") || result.bin.starts_with("0X"))
        result.bin = result.bin.substr(2);

    return result;
}

// ---- Cache operations ----

bool ContractCompiler::saveToCache(
    std::string_view contractName, std::string_view abi, std::string_view bin)
{
    try
    {
        fs::path dir(COMPILED_DIR);
        if (!fs::exists(dir))
            fs::create_directories(dir);

        // Save ABI
        std::ofstream abiFile(dir / (std::string(contractName) + ".abi"));
        if (!abiFile) return false;
        abiFile << abi;

        // Save BIN
        std::ofstream binFile(dir / (std::string(contractName) + ".bin"));
        if (!binFile) return false;
        binFile << bin;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

CachedContract ContractCompiler::loadFromCache(std::string_view contractName)
{
    CachedContract result;
    fs::path dir(COMPILED_DIR);
    if (!fs::exists(dir))
        return result;

    auto abiPath = dir / (std::string(contractName) + ".abi");
    auto binPath = dir / (std::string(contractName) + ".bin");
    auto addrPath = dir / (std::string(contractName) + ".address");

    if (!fs::exists(abiPath) || !fs::exists(binPath))
        return result;

    // Read ABI
    std::ifstream af(abiPath.string());
    if (!af) return result;
    std::ostringstream abiBuf;
    abiBuf << af.rdbuf();
    result.abi = abiBuf.str();

    // Read BIN
    std::ifstream bf(binPath.string());
    if (!bf) return result;
    std::ostringstream binBuf;
    binBuf << bf.rdbuf();
    result.bin = binBuf.str();
    boost::trim(result.bin);

    // Read address (optional)
    if (fs::exists(addrPath))
    {
        std::ifstream addrF(addrPath.string());
        if (addrF)
        {
            std::ostringstream addrBuf;
            addrBuf << addrF.rdbuf();
            result.address = addrBuf.str();
            boost::trim(result.address);
        }
    }

    result.found = true;
    return result;
}

std::vector<std::string> ContractCompiler::listCached()
{
    std::vector<std::string> names;
    fs::path dir(COMPILED_DIR);
    if (!fs::exists(dir))
        return names;

    for (auto& entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".abi")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

bool ContractCompiler::saveDeployAddress(
    std::string_view contractName, std::string_view address)
{
    try
    {
        fs::path dir(COMPILED_DIR);
        if (!fs::exists(dir))
            fs::create_directories(dir);

        auto addrPath = dir / (std::string(contractName) + ".address");

        // Read existing addresses
        std::vector<std::string> addresses;
        if (fs::exists(addrPath))
        {
            std::ifstream f(addrPath.string());
            std::string line;
            while (std::getline(f, line))
            {
                boost::trim(line);
                if (!line.empty())
                    addresses.push_back(line);
            }
        }

        // Prepend new address (newest first)
        addresses.insert(addresses.begin(), std::string(address));

        // Keep last 100
        if (addresses.size() > 100)
            addresses.resize(100);

        // Write back
        std::ofstream out(addrPath.string());
        if (!out) return false;
        for (auto const& a : addresses)
            out << a << '\n';

        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::vector<std::string> ContractCompiler::getDeployAddresses(
    std::string_view contractName, int limit)
{
    std::vector<std::string> addresses;
    fs::path addrPath =
        fs::path(COMPILED_DIR) / (std::string(contractName) + ".address");

    if (!fs::exists(addrPath))
        return addresses;

    std::ifstream f(addrPath.string());
    std::string line;
    while (std::getline(f, line) && static_cast<int>(addresses.size()) < limit)
    {
        boost::trim(line);
        if (!line.empty())
            addresses.push_back(line);
    }
    return addresses;
}
