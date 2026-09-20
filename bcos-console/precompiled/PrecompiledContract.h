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
 * @brief: PrecompiledContract — ABI encoding wrapper over bcos-sdk
 *         ContractABICodec. Uses JSON ABI + JSON params to produce
 *         Solidity ABI-encoded call data.
 * @file: PrecompiledContract.h
 */

#pragma once

#include "PrecompiledContractInfo.h"

#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABIDefinitionFactory.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::console::precompiled
{

/// Info about a single precompiled method
struct MethodInfo
{
    std::string name;
    std::string signature;          // e.g. "addSealer(string,uint256)"
    std::string methodID;           // hex selector, e.g. "0x4c3e8f5b"
    std::vector<std::string> inputTypes;   // e.g. ["string", "uint256"]
    std::vector<std::string> outputTypes;  // e.g. ["int32"]
    size_t inputCount = 0;
};

/**
 * @brief Encodes precompiled contract method calls using the Solidity ABI,
 *        with ABI introspection powered by ContractABIDefinitionFactory.
 */
class PrecompiledContract
{
public:
    using Ptr = std::shared_ptr<PrecompiledContract>;

    explicit PrecompiledContract(bcos::crypto::Hash::Ptr hashImpl)
      : m_hashImpl(std::move(hashImpl))
    {}

    // ---- Encoding / Decoding ----

    bcos::bytes encode(
        PrecompiledType type, std::string_view method, std::string_view jsonParams);

    std::string encodeHex(
        PrecompiledType type, std::string_view method, std::string_view jsonParams);

    std::string decodeOutput(
        PrecompiledType type, std::string_view method, bcos::bytesConstRef output);

    // ---- ABI Introspection (Phase 2) ----

    /// Get full info about a method (signature, types, selector)
    MethodInfo getMethodInfo(PrecompiledType type, std::string_view methodName);

    /// List all method names for a precompiled contract
    std::vector<std::string> listMethods(PrecompiledType type);

    /// Generate a human-readable usage string, e.g.
    /// "addSealer(string nodeID, uint256 weight) → int32"
    std::string getUsage(PrecompiledType type, std::string_view methodName);

    /// Validate that the parameter count matches the ABI definition.
    /// Returns empty string on success, or error message on mismatch.
    std::string validateParamCount(
        PrecompiledType type, std::string_view methodName, size_t paramCount);

    // ---- Accessors ----

    static std::string address(PrecompiledType type)
    {
        return std::string(addressOf(type));
    }

    static const char* abi(PrecompiledType type) { return abiOf(type); }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    bcos::cppsdk::abi::ContractABIDefinitionFactory m_abiFactory;

    // Lazily-created per-ABI codecs
    std::unordered_map<std::string,
        std::unique_ptr<bcos::cppsdk::abi::ContractABICodec>>
        m_codecs;

    // Lazily-parsed ABI definitions
    std::unordered_map<std::string,
        bcos::cppsdk::abi::ContractABIDefinition::Ptr>
        m_definitions;

    bcos::cppsdk::abi::ContractABICodec& getCodec(const char* abiJson);
    bcos::cppsdk::abi::ContractABIDefinition::Ptr getDefinition(const char* abiJson);
};

}  // namespace bcos::console::precompiled
