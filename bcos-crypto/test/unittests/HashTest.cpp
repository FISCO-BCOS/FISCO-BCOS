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
 * @brief test cases for keccak256/sm3
 * @file HashTest.h
 * @date 2021.03.04
 */
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/hash/Sha3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace crypto;
using namespace std::string_view_literals;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(HashTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testKeccak256)
{
    auto keccak256 = std::make_shared<Keccak256>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(keccak256, nullptr, nullptr);
    // test multiple-thread
    auto worker = std::make_shared<ThreadPool>("testHash", 8);
    std::atomic<int64_t> totolCount = 1000;
    worker->enqueue([&]() {
        if (totolCount <= 0)
        {
            return;
        }
        while (totolCount > 0)
        {
            totolCount -= 1;
            auto data = "abcde" + std::to_string(totolCount.load());
            keccak256->hash(bytesConstRef(data));
        }
    });
    while (totolCount > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::string ts = keccak256->emptyHash().hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));

    std::string hashData = "abcde";

    ts = keccak256->hash(bytesConstRef(hashData)).hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("6377c7e66081cb65e473c1b95db5195a27d04a7108b468890224bedbe1a8a6eb"));

    h256 emptyKeccak256(
        *fromHexString("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(emptyKeccak256, keccak256->emptyHash());

    BOOST_REQUIRE_EQUAL(cryptoSuite->hash(""sv),
        h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
    BOOST_REQUIRE_EQUAL(cryptoSuite->hash("hello"sv),
        h256("1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8"));
}
BOOST_AUTO_TEST_CASE(testSM3)
{
    auto sm3 = std::make_shared<SM3>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(sm3, nullptr, nullptr);
    std::string ts = sm3->emptyHash().hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));

    std::string hashData = "abcde";
    ts = sm3->hash(bytesConstRef(hashData)).hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("afe4ccac5ab7d52bcae36373676215368baf52d3905e1fecbe369cc120e97628"));

    h256 emptySM3(
        *fromHexString("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));
    BOOST_REQUIRE_EQUAL(emptySM3, sm3->emptyHash());

    BOOST_REQUIRE_EQUAL(cryptoSuite->hash(""sv),
        h256("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b"));

    BOOST_REQUIRE_EQUAL(cryptoSuite->hash("hello"sv),
        h256("becbbfaae6548b8bf0cfcad5a27183cd1be6093b1cceccc303d9c61d0a645268"));
}
BOOST_AUTO_TEST_CASE(testSha3)
{
    auto sha3 = std::make_shared<class Sha3>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(sha3, nullptr, nullptr);
    std::string ts = sha3->emptyHash().hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));

    std::string hashData = "abcde";
    ts = sha3->hash(bytesConstRef(hashData)).hex();
    BOOST_CHECK_EQUAL(
        ts, std::string("d716ec61e18904a8f58679b71cb065d4d5db72e0e0c3f155a4feff7add0e58eb"));

    h256 emptySha3(
        *fromHexString("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));
    BOOST_REQUIRE_EQUAL(emptySha3, sha3->emptyHash());

    BOOST_REQUIRE_EQUAL(cryptoSuite->hash(""sv),
        h256("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));

    BOOST_REQUIRE_EQUAL(cryptoSuite->hash("hello"sv),
        h256("3338be694f50c5f338814986cdf0686453a888b84f424d792af4b9202398f392"));
}

BOOST_AUTO_TEST_CASE(SHA256_test)
{
    HashType hash;
    auto sha256 = std::make_shared<Sha256>();
    bcos::bytes input{};
    hash = sha256->hash(bcos::ref(input));
    BCOS_LOG(DEBUG) << "hash: " << hash.hex();
    BOOST_CHECK_EQUAL(
        hash.hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    std::string data =
        "123456781234567812345678123456781234567812345678123456781234567812345678123456781234567812"
        "3456781234567812345678";
    input += bcos::fromHex(data);
    hash = sha256->hash(bcos::ref(input));
    BCOS_LOG(DEBUG) << "hash: " << hash.hex();
    BOOST_REQUIRE_EQUAL(
        hash.hex(), "7303caef875be8c39b2c2f1905ea24adcc024bef6830a965fe05370f3170dc52");
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
