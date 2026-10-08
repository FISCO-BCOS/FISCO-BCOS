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
 * @file OsakaFixtureManifestTest.cpp
 * @brief Verify the pinned EEST osaka precompile fixtures against their manifest
 *        sha256 records — the checksums were decorative until this check existed.
 */

#include <boost/test/unit_test.hpp>

#include <bcos-crypto/hash/Sha256.h>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::filesystem::path fixtureDir()
{
    return std::filesystem::path(OPSTACK_TEST_SOURCE_ROOT) / "bcos-evm/test/opstack/fixtures/osaka";
}

std::string sha256Hex(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in.good(), "cannot open fixture " << path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const auto content = buffer.str();
    const auto digest = bcos::crypto::sha256Hash(
        bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(content.data()), content.size()));
    return digest.hex();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OsakaFixtureManifestTest)

BOOST_AUTO_TEST_CASE(ManifestRecordsEveryFixture)
{
    const auto manifestPath = fixtureDir() / "manifest.json";
    std::ifstream in(manifestPath);
    if (!in.good())
    {
        // The manifest is metadata for a not-yet-wired consumer; absence of the whole
        // directory (e.g. stripped checkout) is out of scope here.
        BOOST_TEST_MESSAGE("osaka fixture manifest not present; skipping");
        return;
    }
    Json::Reader reader;
    Json::Value manifest;
    BOOST_REQUIRE_MESSAGE(reader.parse(in, manifest), "cannot parse manifest.json");

    BOOST_REQUIRE(manifest.isArray());
    std::size_t checked = 0;
    for (auto const& entry : manifest)
    {
        BOOST_REQUIRE(entry.isMember("file"));
        BOOST_REQUIRE(entry.isMember("sha256"));
        const auto path = fixtureDir() / entry["file"].asString();
        BOOST_REQUIRE_MESSAGE(
            std::filesystem::exists(path), entry["file"].asString() << ": recorded but absent");
        // The recorded checksum must match the shipped bytes — until this check the
        // sha256 column was decorative (a re-exported/corrupted fixture passed).
        BOOST_CHECK_MESSAGE(entry["sha256"].asString() == sha256Hex(path),
            entry["file"].asString() << ": sha256 mismatch (re-exported or corrupted)");
        ++checked;
    }
    BOOST_CHECK_MESSAGE(checked > 0, "manifest lists no fixtures");
}

BOOST_AUTO_TEST_SUITE_END()
