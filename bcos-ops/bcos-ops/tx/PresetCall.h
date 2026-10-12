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
 * @brief running a Preset: CLI strings → ABI-encoded call → send or call → decoded result with
 *        the precompiled's int32 code mapped to its name. Shared by the CLI groups and the TUI
 *        send page; neither encodes anything itself.
 * @file PresetCall.h
 */
#pragma once

#include "Presets.h"
#include "TxSender.h"
#include <json/json.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops
{
/// name of a negative code a precompiled writes into its output (precompiled/common/Common.h);
/// "" when unknown
std::string precompiledCodeName(int32_t _code);

/// positional CLI values → JSON array text for Abi::encodeMethodBySignature, typed by the
/// preset's params: bytes / bytesN accept `0x…` and become the SDK's `hex://…`; numbers, bools
/// and arrays keep their JSON type; strings and addresses stay strings. Throws OpsError{1} on
/// a wrong count or a malformed bytes value.
std::string encodePresetArgs(Preset const& _preset, std::vector<std::string> const& _args);
/// `0x12345678` → `hex://12345678` (also accepts the already converted form)
std::string toSdkBytes(std::string const& _hexValue);

struct PresetResult
{
    std::string mode = "direct";  // direct | proposal (ticket 04 fills the latter)
    std::string to;
    std::string method;
    Json::Value params{Json::arrayValue};
    Receipt receipt;
    Json::Value decoded;  // output decoded through the preset's ABI fragment (null when none)
    std::optional<int32_t> code;  // int32 code from the output when the preset returns one
    std::string codeName;
    bool ok = false;  // receipt status 0 and (when the preset returns a code) code == 0
    int exitCode() const;
    Json::Value toJson() const;
};

/// decodes a receipt (or a call result) of _preset
PresetResult decodePresetResult(Preset const& _preset, Receipt const& _receipt, bool _sm);

/// the one place that decides direct vs proposal (ticket 04) and runs the call
class PresetRunner
{
public:
    PresetRunner(Sender& _sender, bool _sm) : m_sender(_sender), m_sm(_sm) {}
    /// _to overrides the preset's `to` (sentinels, HelloWorld address); write presets are sent,
    /// read presets are called
    PresetResult run(
        Preset const& _preset, std::vector<std::string> const& _args, std::string const& _to = "");
    /// the encoded call data, for the confirmation layer and tests
    bytes encode(Preset const& _preset, std::vector<std::string> const& _args) const;

private:
    Sender& m_sender;
    bool m_sm;
};
}  // namespace bcos::ops
