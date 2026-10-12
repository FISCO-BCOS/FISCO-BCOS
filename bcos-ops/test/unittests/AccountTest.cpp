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
 * @brief account files: pem and hex private keys load to the same address, `account new`
 *        writes files that load back, bad inputs are refused
 * @file AccountTest.cpp
 */
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
struct TempDir
{
    std::filesystem::path dir;
    TempDir()
    {
        dir = std::filesystem::temp_directory_path() /
              ("bcos_ops_account_" + std::to_string(::getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~TempDir() { std::filesystem::remove_all(dir); }
    std::string write(std::string const& _name, std::string const& _content)
    {
        auto path = (dir / _name).string();
        std::ofstream out(path);
        out << _content;
        return path;
    }
};

std::string hexOf(bcos::crypto::KeyPairInterface& _keyPair)
{
    return toHex(_keyPair.secretKey()->data());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AccountTest)

BOOST_AUTO_TEST_CASE(generatedPemLoadsBackToTheSameAddress)
{
    for (bool sm : {false, true})
    {
        TempDir temp;
        auto keyPair = makeAccount(sm, std::nullopt);
        auto address = accountAddress(sm, *keyPair);
        BOOST_CHECK_EQUAL(address.size(), 42U);
        BOOST_CHECK(address.starts_with("0x"));
        auto files = writePemKeyPair((temp.dir / "accounts").string(), sm, *keyPair);
        BOOST_CHECK_EQUAL(
            files.privateKeyFile, (temp.dir / "accounts" / (address + ".pem")).string());
        BOOST_CHECK_EQUAL(
            files.publicKeyFile, (temp.dir / "accounts" / (address + ".public.pem")).string());
        BOOST_CHECK(std::filesystem::exists(files.privateKeyFile));
        BOOST_CHECK(std::filesystem::exists(files.publicKeyFile));
        auto perms = std::filesystem::status(files.privateKeyFile).permissions();
        BOOST_CHECK(
            (perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
            std::filesystem::perms::none);
        std::ifstream pem(files.privateKeyFile);
        std::string firstLine;
        std::getline(pem, firstLine);
        BOOST_CHECK_EQUAL(firstLine, "-----BEGIN EC PRIVATE KEY-----");
        auto loaded = makeAccount(sm, files.privateKeyFile);
        BOOST_CHECK_EQUAL(accountAddress(sm, *loaded), address);
        BOOST_CHECK_EQUAL(hexOf(*loaded), hexOf(*keyPair));
        // the public pem must hold the same point as the key pair (x||y, 64 bytes)
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(
            BIO_new_file(files.publicKeyFile.c_str(), "r"), BIO_free);
        BOOST_REQUIRE(bio);
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pub(
            PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
        BOOST_REQUIRE(pub);
        std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec(
            EVP_PKEY_get1_EC_KEY(pub.get()), EC_KEY_free);
        BOOST_REQUIRE(ec);
        std::vector<unsigned char> point(65, 0);
        auto len = EC_POINT_point2oct(EC_KEY_get0_group(ec.get()), EC_KEY_get0_public_key(ec.get()),
            POINT_CONVERSION_UNCOMPRESSED, point.data(), point.size(), nullptr);
        BOOST_REQUIRE_EQUAL(len, 65U);
        auto const& expected = keyPair->publicKey()->data();
        BOOST_REQUIRE_EQUAL(expected.size(), 64U);
        BOOST_CHECK(std::equal(expected.begin(), expected.end(), point.begin() + 1));
    }
}

BOOST_AUTO_TEST_CASE(rewrittenKeyFileIsTightenedTo0600)
{
    TempDir temp;
    auto keyPair = makeAccount(false, std::nullopt);
    auto address = accountAddress(false, *keyPair);
    auto dir = temp.dir / "accounts";
    std::filesystem::create_directories(dir);
    auto target = dir / (address + ".pem");
    std::ofstream(target) << "old\n";
    std::filesystem::permissions(
        target, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                    std::filesystem::perms::group_read | std::filesystem::perms::others_read);
    auto files = writePemKeyPair(dir.string(), false, *keyPair);
    auto perms = std::filesystem::status(files.privateKeyFile).permissions();
    BOOST_CHECK((perms & (std::filesystem::perms::group_all |
                             std::filesystem::perms::others_all)) == std::filesystem::perms::none);
    BOOST_CHECK_EQUAL(accountAddress(false, *makeAccount(false, files.privateKeyFile)), address);
}

BOOST_AUTO_TEST_CASE(hexAndPemGiveTheSameAddress)
{
    for (bool sm : {false, true})
    {
        TempDir temp;
        auto keyPair = makeAccount(sm, std::nullopt);
        auto address = accountAddress(sm, *keyPair);
        auto hex = hexOf(*keyPair);
        BOOST_REQUIRE_EQUAL(hex.size(), 64U);
        auto plain = makeAccount(sm, temp.write("plain.key", hex));
        auto prefixed = makeAccount(sm, temp.write("prefixed.key", "0x" + hex + "\n"));
        auto padded = makeAccount(sm, temp.write("padded.key", "  " + hex + "\r\n"));
        BOOST_CHECK_EQUAL(accountAddress(sm, *plain), address);
        BOOST_CHECK_EQUAL(accountAddress(sm, *prefixed), address);
        BOOST_CHECK_EQUAL(accountAddress(sm, *padded), address);
    }
}

BOOST_AUTO_TEST_CASE(badInputsAreRefused)
{
    TempDir temp;
    auto keyPair = makeAccount(false, std::nullopt);
    auto hex = hexOf(*keyPair);
    BOOST_CHECK_THROW(makeAccount(false, temp.write("short.key", hex.substr(1))), OpsError);
    BOOST_CHECK_THROW(makeAccount(false, temp.write("nonhex.key", "zz" + hex.substr(2))), OpsError);
    BOOST_CHECK_THROW(makeAccount(false, temp.write("empty.key", "\n")), OpsError);
    BOOST_CHECK_THROW(makeAccount(false, temp.write("garbage.pem",
                                             "-----BEGIN EC PRIVATE KEY-----\nnotbase64\n"
                                             "-----END EC PRIVATE KEY-----\n")),
        OpsError);
    BOOST_CHECK_THROW(makeAccount(false, (temp.dir / "missing.pem").string()), OpsError);
    BOOST_CHECK_THROW(loadPemPrivateKey(temp.write("hex-as-pem.key", hex)), OpsError);
}

BOOST_AUTO_TEST_CASE(accountNewCommandWritesBothFiles)
{
    for (bool sm : {false, true})
    {
        TempDir temp;
        auto outDir = (temp.dir / "accounts").string();
        std::ostringstream out;
        std::ostringstream err;
        std::vector<std::string> argv = {"fisco-bcos", "account", "new", "--out", outDir, "--json"};
        if (sm)
        {
            argv.push_back("--sm");
        }
        BOOST_REQUIRE_EQUAL(runOps(argv, out, err), c_exitOk);
        auto json = parseJson(out.str(), "account new");
        auto address = json["address"].asString();
        BOOST_CHECK_EQUAL(address.size(), 42U);
        BOOST_CHECK_EQUAL(json["sm"].asBool(), sm);
        BOOST_CHECK(std::filesystem::exists(json["privateKeyFile"].asString()));
        BOOST_CHECK(std::filesystem::exists(json["publicKeyFile"].asString()));
        BOOST_CHECK_EQUAL(json["privateKeyFile"].asString(), outDir + "/" + address + ".pem");
        auto loaded = makeAccount(sm, json["privateKeyFile"].asString());
        BOOST_CHECK_EQUAL(accountAddress(sm, *loaded), address);
    }
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos", "account", "list"}, out, err), c_exitUsage);
    BOOST_CHECK(err.str().find("usage: account new") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
