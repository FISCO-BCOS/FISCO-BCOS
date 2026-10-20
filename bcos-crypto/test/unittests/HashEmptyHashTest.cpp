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
 * @file HashEmptyHashTest.cpp
 * @brief Issue #5578: Hash::emptyHash() is const (usable through the `const Hash&` HostContext
 *        holds) and answers hash("") for every implementation, including under concurrent first
 *        use (the former lazily-written member raced; the function-local static does not).
 */
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/hash/Sha3.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <string_view>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(HashEmptyHashTest, TestPromptFixture)

namespace
{
void checkEmptyHash(const Hash& hashImpl, std::string_view expectedHex)
{
    // Through a const reference: the accessor must be const.
    BOOST_CHECK_EQUAL(hashImpl.emptyHash(), hashImpl.hash(bytesConstRef{}));
    BOOST_CHECK_EQUAL(hashImpl.emptyHash().hex(), expectedHex);

    // Concurrent first use from a fresh instance returns one value everywhere.
    std::vector<std::thread> threads;
    std::vector<HashType> seen(8);
    for (size_t i = 0; i < seen.size(); ++i)
    {
        threads.emplace_back([&hashImpl, &seen, i]() { seen[i] = hashImpl.emptyHash(); });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    for (auto const& value : seen)
    {
        BOOST_CHECK_EQUAL(value, hashImpl.emptyHash());
    }
}
}  // namespace

BOOST_AUTO_TEST_CASE(emptyHashMatchesHashOfEmptyInputPerImplementation)
{
    const Keccak256 keccak256;
    const Sha256 sha256;
    const class Sha3 sha3;  // `Sha3` is also a HashImplType enumerator in this namespace
    const SM3 sm3;
    checkEmptyHash(keccak256, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    checkEmptyHash(sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    checkEmptyHash(sha3, "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
    checkEmptyHash(sm3, "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
