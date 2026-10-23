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
 *  limitations under the License. *
 * @brief every function selector of the embedded bcos-auth ABIs must appear in the node's
 *        CommitteeBin.h bytecode (keccak for committeeBin, sm3 for committeeSmBin), and the
 *        preset table's committee rows must be functions of those ABIs
 * @file AuthAbiTest.cpp
 */
#include "bcos-ops/tx/AuthAbi.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/tx/Presets.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-executor/src/precompiled/extension/CommitteeBin.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <set>

namespace bcos::ops::test
{
namespace
{
std::string canonicalType(Json::Value const& _param)
{
    auto type = _param["type"].asString();
    if (!type.starts_with("tuple"))
    {
        return type;
    }
    std::string inner = "(";
    for (Json::ArrayIndex i = 0; i < _param["components"].size(); ++i)
    {
        inner += (i == 0 ? "" : ",") + canonicalType(_param["components"][i]);
    }
    return inner + ")" + type.substr(5);  // tuple[] → (...)[]
}

std::vector<std::string> functionSignatures(std::string_view _abi)
{
    std::vector<std::string> signatures;
    for (auto const& entry : parseJson(_abi, "abi"))
    {
        if (entry["type"].asString() != "function")
        {
            continue;
        }
        std::string signature = entry["name"].asString() + "(";
        for (Json::ArrayIndex i = 0; i < entry["inputs"].size(); ++i)
        {
            signature += (i == 0 ? "" : ",") + canonicalType(entry["inputs"][i]);
        }
        signatures.push_back(signature + ")");
    }
    return signatures;
}

std::string selectorHex(std::string const& _signature, bool _sm)
{
    auto hash = _sm ? bcos::crypto::SM3().hash(bytesConstRef(_signature)) :
                      bcos::crypto::Keccak256().hash(bytesConstRef(_signature));
    return hash.hex().substr(0, 8);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AuthAbiTest)

BOOST_AUTO_TEST_CASE(everySelectorIsInTheGenesisBytecode)
{
    size_t checked = 0;
    for (auto abi : {authabi::c_committeeManager, authabi::c_proposalManager, authabi::c_committee})
    {
        auto signatures = functionSignatures(abi);
        BOOST_CHECK(signatures.size() >= 8);
        for (auto const& signature : signatures)
        {
            BOOST_CHECK_MESSAGE(
                bcos::committeeBin.find(selectorHex(signature, false)) != std::string_view::npos,
                "keccak selector of " + signature + " not in committeeBin");
            BOOST_CHECK_MESSAGE(
                bcos::committeeSmBin.find(selectorHex(signature, true)) != std::string_view::npos,
                "sm3 selector of " + signature + " not in committeeSmBin");
            ++checked;
        }
    }
    BOOST_CHECK_EQUAL(checked, 15U + 16U + 12U);  // CommitteeManager / ProposalManager / Committee
}

BOOST_AUTO_TEST_CASE(committeePresetRowsAreAbiFunctions)
{
    std::set<std::string> known;
    for (auto abi : {authabi::c_committeeManager, authabi::c_proposalManager, authabi::c_committee})
    {
        for (auto const& signature : functionSignatures(abi))
        {
            known.insert(signature);
        }
    }
    size_t rows = 0;
    for (auto const* preset : presetsOf("committee"))
    {
        BOOST_CHECK_MESSAGE(
            known.contains(std::string(preset->signature)), std::string(preset->signature));
        ++rows;
    }
    BOOST_CHECK(rows >= 20);
    // every proposal form a precompiled row points at is a CommitteeManager function
    for (auto const& preset : allPresets())
    {
        if (preset.proposal)
        {
            BOOST_CHECK_MESSAGE(known.contains(std::string(preset.proposal->signature)),
                std::string(preset.proposal->signature));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
