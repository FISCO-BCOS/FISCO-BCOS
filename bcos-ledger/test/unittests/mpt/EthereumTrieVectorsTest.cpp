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
 * @file EthereumTrieVectorsTest.cpp
 * @brief Anchors computeTrieRoot, the incremental commitTrie path and batched multi-key
 *        merges to the official ethereum/tests TrieTests secure-trie vectors, read from the
 *        ethereum-tests data port
 *        (ETHEREUM_TESTS_DIR, installed by vcpkg under the tests manifest feature). Unlike the
 *        in-repo reference trie (TestHelpers.h), which shares authorship with the
 *        implementation, these roots were produced and cross-checked by the Ethereum clients,
 *        independently of this codebase.
 *
 *        Only the secure-trie vector files are used: this MPT stores fixed 64-nibble keys
 *        exclusively (TrieMerge.h equal-length invariant), so the variable-length-key files
 *        (trietest.json, trieanyorder.json) do not apply.
 */
#include "TestHelpers.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
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

BOOST_AUTO_TEST_SUITE(EthereumTrieVectorsSuite)

namespace
{
// Unique `etv*` names avoid an anonymous-namespace ODR clash with the other mpt test files
// under UNITY_BUILD (same convention as ComputeTrieRootTest's `ctr*`).
using EtvNodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

// "ETH" in ASCII — an arbitrary fixed seed so the shuffled-order replay is reproducible.
constexpr uint32_t c_etvShuffleSeed = 0x455448;

/// One vector operation: the raw (pre-keccak) key and its value; nullopt records a delete.
struct EtvOp
{
    bcos::bytes key;
    std::optional<bcos::bytes> value;
};

struct EtvCase
{
    std::string name;
    std::vector<EtvOp> ops;
    bcos::h256 expectedRoot;
};

/// TrieTests string convention: "0x"-prefixed strings decode as hex bytes, anything else is
/// taken as raw ASCII bytes.
bcos::bytes etvDecode(std::string const& text)
{
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        return bcos::fromHex(text);
    }
    return {text.begin(), text.end()};
}

/// The secure-trie key transform: leaf path = keccak256(raw key bytes), so every path is the
/// fixed 64 nibbles this MPT requires.
bcos::h256 etvSecureKey(bcos::bytes const& rawKey)
{
    return bcos::crypto::keccak256Hash(bcos::ref(rawKey));
}

/// Read one vector file from the ethereum-tests data port's TrieTests/ directory.
/// ETHEREUM_TESTS_DIR is injected by bcos-ledger/test/CMakeLists.txt; the port itself is
/// installed by vcpkg under the tests manifest feature (TESTS=ON).
std::string etvReadVectorFile(std::string const& fileName)
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

Json::Value etvParseJson(std::string const& text)
{
    Json::CharReaderBuilder builder;
    std::istringstream stream{text};
    Json::Value parsed;
    std::string errors;
    BOOST_REQUIRE_MESSAGE(Json::parseFromStream(builder, stream, &parsed, &errors), errors);
    return parsed;
}

/// Load every case of one vector file. "in" is either an ordered [key, value|null] list
/// (trietest_secureTrie) or an unordered {key: value} object (anyorder / hex_encoded).
std::vector<EtvCase> etvLoad(std::string const& fileName)
{
    auto const parsed = etvParseJson(etvReadVectorFile(fileName));
    std::vector<EtvCase> cases;
    for (auto const& name : parsed.getMemberNames())
    {
        auto const& caseJson = parsed[name];
        EtvCase testCase{
            .name = name, .ops = {}, .expectedRoot = bcos::h256{caseJson["root"].asString()}};
        auto const& input = caseJson["in"];
        if (input.isArray())
        {
            for (auto const& pair : input)
            {
                testCase.ops.push_back({.key = etvDecode(pair[0].asString()),
                    .value = pair[1].isNull() ? std::nullopt :
                                                std::make_optional(etvDecode(pair[1].asString()))});
            }
        }
        else
        {
            for (auto const& key : input.getMemberNames())
            {
                testCase.ops.push_back(
                    {.key = etvDecode(key), .value = etvDecode(input[key].asString())});
            }
        }
        cases.push_back(std::move(testCase));
    }
    BOOST_REQUIRE(!cases.empty());
    return cases;
}

/// The final keyHash -> value state after applying @p ops in order (a delete erases).
std::map<bcos::h256, bcos::bytes> etvFinalState(std::vector<EtvOp> const& ops)
{
    std::map<bcos::h256, bcos::bytes> state;
    for (auto const& operation : ops)
    {
        if (operation.value.has_value())
        {
            state[etvSecureKey(operation.key)] = *operation.value;
        }
        else
        {
            state.erase(etvSecureKey(operation.key));
        }
    }
    return state;
}

/// Replay @p ops one commit each: the first commit takes the from-empty path, every later one
/// the incremental mergeTrie path (including leaf deletes and branch collapse), and return the
/// final root.
bcos::h256 etvIncrementalRoot(std::vector<EtvOp> const& ops)
{
    EtvNodeStorage storage;
    auto root = emptyRootHash();
    for (auto const& operation : ops)
    {
        root =
            commitTrieFlushed(storage, root, {{etvSecureKey(operation.key), operation.value}}).root;
    }
    return root;
}

/// Replay @p ops in batches of up to @p batchSize keys, one commitTrieFlushed per batch. This
/// is the production shape — a block's aggregated changeset lands in a single multi-key
/// mergeTrie call, inserts and deletes mixed — which neither the single-op path above nor the
/// stateless computeTrieRoot exercises. A later op on a key already in the batch overwrites it,
/// exactly the last-write-wins semantics of aggregating a block's changes.
bcos::h256 etvBatchedRoot(std::vector<EtvOp> const& ops, size_t batchSize)
{
    EtvNodeStorage storage;
    auto root = emptyRootHash();
    std::map<bcos::h256, std::optional<bcos::bytes>> batch;
    for (auto const& operation : ops)
    {
        batch[etvSecureKey(operation.key)] = operation.value;
        if (batch.size() >= batchSize)
        {
            root = commitTrieFlushed(storage, root, batch).root;
            batch.clear();
        }
    }
    if (!batch.empty())
    {
        root = commitTrieFlushed(storage, root, batch).root;
    }
    return root;
}

/// All build paths against the official expected root, per case: stateless full build,
/// one-op-per-commit incremental, and batched multi-key merges at several batch sizes (the
/// last size folds the whole case into one commit).
void etvCheckAll(std::string const& fileName)
{
    for (auto const& testCase : etvLoad(fileName))
    {
        BOOST_TEST_CONTEXT(testCase.name)
        {
            BOOST_CHECK_EQUAL(
                computeTrieRoot(etvFinalState(testCase.ops)).root, testCase.expectedRoot);
            BOOST_CHECK_EQUAL(etvIncrementalRoot(testCase.ops), testCase.expectedRoot);
            for (auto const batchSize : {size_t{3}, size_t{8}, testCase.ops.size()})
            {
                BOOST_TEST_CONTEXT("batchSize=" << batchSize)
                {
                    BOOST_CHECK_EQUAL(
                        etvBatchedRoot(testCase.ops, batchSize), testCase.expectedRoot);
                }
            }
        }
    }
}

}  // namespace

// hex_encoded_securetrie_test.json: 20-byte address keys with Ethereum account-RLP values — the
// exact key/value shape the state trie stores in production.
BOOST_AUTO_TEST_CASE(HexEncodedSecureTrie)
{
    etvCheckAll("hex_encoded_securetrie_test.json");
}

// trietest_secureTrie.json: ordered op sequences with null deletes. branchingTests inserts 25
// keys then deletes all 25 — its official root IS the empty-trie root, so the incremental path
// must collapse the whole trie back to emptyRootHash() step by step.
BOOST_AUTO_TEST_CASE(OrderedOpsSecureTrie)
{
    etvCheckAll("trietest_secureTrie.json");

    auto const cases = etvLoad("trietest_secureTrie.json");
    auto const branching = std::ranges::find_if(
        cases, [](EtvCase const& testCase) { return testCase.name == "branchingTests"; });
    BOOST_REQUIRE(branching != cases.end());
    BOOST_CHECK_EQUAL(branching->expectedRoot, emptyRootHash());
}

// trieanyorder_secureTrie.json: insertion order must not matter. Beyond the file's own claim,
// replay each set reversed and in one fixed-seed shuffle through the incremental path.
BOOST_AUTO_TEST_CASE(AnyOrderSecureTrie)
{
    etvCheckAll("trieanyorder_secureTrie.json");

    for (auto const& testCase : etvLoad("trieanyorder_secureTrie.json"))
    {
        BOOST_TEST_CONTEXT(testCase.name)
        {
            auto reversed = testCase.ops;
            std::ranges::reverse(reversed);
            BOOST_CHECK_EQUAL(etvIncrementalRoot(reversed), testCase.expectedRoot);

            auto shuffled = testCase.ops;
            auto rng = seededRng(c_etvShuffleSeed);
            std::ranges::shuffle(shuffled, rng);
            BOOST_CHECK_EQUAL(etvIncrementalRoot(shuffled), testCase.expectedRoot);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
