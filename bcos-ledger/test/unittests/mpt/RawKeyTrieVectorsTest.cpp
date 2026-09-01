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
 * @file RawKeyTrieVectorsTest.cpp
 * @brief Anchors computeTrieRootFromRawKeys (the non-secure, raw-byte-key trie) to the official
 *        ethereum/tests TrieTests non-secure vector files — trietest.json (ordered ops with
 *        null deletes) and trieanyorder.json (unordered key/value objects).
 *
 *        Unlike the secure-trie files (fixed 64-nibble keccak keys), these exercise
 *        variable-length byte keys, including proper-prefix keys that terminate inside a branch
 *        node (emptyValues / insert-middle-leaf / branch-value-update) — the BranchNode.value
 *        path that the raw-key builder must support. The roots here are the canonical Ethereum
 *        client outputs, independent of this codebase.
 */
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(RawKeyTrieVectorsSuite)

namespace
{
// Unique `rkv*` names avoid an anonymous-namespace ODR clash with the other mpt test files
// under UNITY_BUILD (same convention as the secure-trie suite's `etv*`).

/// One vector operation: the raw (non-hashed) key and its value; nullopt records a delete.
struct RkvOp
{
    bcos::bytes key;
    std::optional<bcos::bytes> value;
};

struct RkvCase
{
    std::string name;
    std::vector<RkvOp> ops;
    bcos::h256 expectedRoot;
};

/// TrieTests string convention: "0x"-prefixed strings decode as hex bytes, anything else is
/// taken as raw ASCII bytes.
bcos::bytes rkvDecode(std::string const& text)
{
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        return bcos::fromHex(text);
    }
    return {text.begin(), text.end()};
}

/// Read one vector file from the ethereum-tests data port's TrieTests/ directory.
/// ETHEREUM_TESTS_DIR is injected by bcos-ledger/test/CMakeLists.txt.
std::string rkvReadVectorFile(std::string const& fileName)
{
    auto const path = std::filesystem::path{ETHEREUM_TESTS_DIR} / "TrieTests" / fileName;
    std::ifstream input{path};
    BOOST_REQUIRE_MESSAGE(input.is_open(), "cannot open " << path
                                                          << " — is the ethereum-tests vcpkg "
                                                             "data port installed? (configure "
                                                             "with -DTESTS=ON)");
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

Json::Value rkvParseJson(std::string const& text)
{
    Json::CharReaderBuilder builder;
    std::istringstream stream{text};
    Json::Value parsed;
    std::string errors;
    BOOST_REQUIRE_MESSAGE(Json::parseFromStream(builder, stream, &parsed, &errors), errors);
    return parsed;
}

/// Load every case of one non-secure vector file.
std::vector<RkvCase> rkvLoad(std::string const& fileName)
{
    auto const parsed = rkvParseJson(rkvReadVectorFile(fileName));
    std::vector<RkvCase> cases;
    for (auto const& name : parsed.getMemberNames())
    {
        auto const& caseJson = parsed[name];
        RkvCase testCase{
            .name = name, .ops = {}, .expectedRoot = bcos::h256{caseJson["root"].asString()}};
        auto const& input = caseJson["in"];
        if (input.isArray())
        {
            for (auto const& pair : input)
            {
                testCase.ops.push_back({.key = rkvDecode(pair[0].asString()),
                    .value = pair[1].isNull() ? std::nullopt :
                                                std::make_optional(rkvDecode(pair[1].asString()))});
            }
        }
        else
        {
            for (auto const& key : input.getMemberNames())
            {
                testCase.ops.push_back(
                    {.key = rkvDecode(key), .value = rkvDecode(input[key].asString())});
            }
        }
        cases.push_back(std::move(testCase));
    }
    BOOST_REQUIRE(!cases.empty());
    return cases;
}

/// Final raw-key → value state after applying @p ops in order (a delete erases).
std::map<bcos::bytes, bcos::bytes> rkvFinalState(std::vector<RkvOp> const& ops)
{
    std::map<bcos::bytes, bcos::bytes> state;
    for (auto const& operation : ops)
    {
        if (operation.value.has_value())
        {
            state[operation.key] = *operation.value;
        }
        else
        {
            state.erase(operation.key);
        }
    }
    return state;
}

/// Replay one vector file's final states through computeTrieRootFromRawKeys and compare against
/// the official roots. The map's std::less<bcos::bytes> is exactly the required ascending raw
/// byte order (the computeTrieRootFromRawKeys contract).
void rkvCheckAll(std::string const& fileName)
{
    for (auto const& testCase : rkvLoad(fileName))
    {
        BOOST_TEST_CONTEXT(testCase.name)
        {
            auto const state = rkvFinalState(testCase.ops);
            std::vector<std::pair<bcos::bytesConstRef, bcos::bytesConstRef>> sorted;
            sorted.reserve(state.size());
            for (auto const& [key, value] : state)
            {
                sorted.emplace_back(bcos::ref(key), bcos::ref(value));
            }
            BOOST_CHECK_EQUAL(computeTrieRootFromRawKeys(sorted).root, testCase.expectedRoot);
        }
    }
}

}  // namespace

// trietest.json: ordered op sequences with null deletes, variable-length raw keys — includes the
// prefix-key cases that require BranchNode.value support.
BOOST_AUTO_TEST_CASE(RawKeyOrderedOps)
{
    rkvCheckAll("trietest.json");
}

// trieanyorder.json: unordered key/value objects; insertion order must not matter (the map sorts).
BOOST_AUTO_TEST_CASE(RawKeyAnyOrder)
{
    rkvCheckAll("trieanyorder.json");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
