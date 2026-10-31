/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief thin wrapper over bcos-cpp-sdk's ContractABICodec with the hash picked by chain type
 * @file Abi.h
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
class Abi
{
public:
    explicit Abi(bool _sm) : m_sm(_sm) {}
    /// _jsonParams is a JSON array text, e.g. ["Hello"]
    bytes encodeConstructor(
        std::string const& _abi, std::string const& _bin, std::string const& _jsonParams) const;
    bytes encodeMethod(
        std::string const& _abi, std::string const& _method, std::string const& _jsonParams) const;
    bytes encodeMethodBySignature(
        std::string const& _signature, std::string const& _jsonParams) const;
    /// returns the decoded output as JSON text
    std::string decodeOutput(
        std::string const& _abi, std::string const& _method, bytes const& _output) const;
    /// decodes a call's input (selector included) by the method signature; JSON array text
    std::string decodeInputBySignature(std::string const& _signature, bytes const& _input) const;

private:
    bool m_sm;
};

/// "007" → "7", "000" → "0"
std::string withoutLeadingZeros(std::string const& _digits);

/// "010", "-07": a decimal integer written with a leading zero. It is not JSON, so it would reach
/// the codec as text, whose u256(str) / s256(str) read a leading 0 as octal ("010" is 8)
bool isZeroPaddedInteger(std::string_view _arg);

/// positional CLI args → JSON array text; each arg that parses as JSON is kept, others are
/// strings. An integer beyond 64 bits stays decimal text (jsoncpp would make it a double); a
/// zero-padded integer (isZeroPaddedInteger) is refused with OpsError{1}, quote it to pass text
std::string argsToJsonArray(std::vector<std::string> const& _args);
}  // namespace bcos::ops
