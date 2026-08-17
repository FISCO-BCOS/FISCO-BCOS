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
 * @brief Regression test for FIB-136: checkSignature() did not catch exceptions
 *        from the signature backend, causing ERROR log spam when a Byzantine peer
 *        flooded malformed signature data.
 * @file FIB136_CheckSignatureNoExceptionTest.cpp
 * @author: claude
 * @date 2026-05-07
 */
#include "bcos-pbft/pbft/engine/PBFTEngine.h"
#include "test/unittests/pbft/PBFTFixture.h"
#include "test/unittests/protocol/FakePBFTMessage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::test;

namespace bcos::test
{

// A PBFTBaseMessage subclass whose verifySignature() always throws to simulate
// a malformed signature from the secp256k1 backend (FIB-136 scenario).
class ThrowingPBFTMessage : public bcos::consensus::PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<ThrowingPBFTMessage>;

    // verifySignature() simulates the secp256k1 backend throwing on malformed data
    bool verifySignature(bcos::crypto::CryptoSuite::Ptr, bcos::crypto::PublicPtr) override
    {
        throw std::invalid_argument("FIB-136 test: malformed signature data");
    }

    // Minimal stubs for the rest of the interface
    int64_t timestamp() const override { return 0; }
    int32_t version() const override { return 0; }
    ViewType view() const override { return 0; }
    IndexType generatedFrom() const override { return 0; }
    int64_t index() const override { return 0; }
    void setIndex(int64_t) override {}
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    PacketType packetType() const override { return PacketType::PrePreparePacket; }
    void setTimestamp(int64_t) override {}
    void setVersion(int32_t) override {}
    void setView(ViewType) override {}
    void setGeneratedFrom(IndexType) override {}
    void setHash(bcos::crypto::HashType const& h) override { m_hash = h; }
    void setPacketType(PacketType) override {}
    bytesPointer encode(
        bcos::crypto::CryptoSuite::Ptr, bcos::crypto::KeyPairInterface::Ptr) const override
    {
        return nullptr;
    }
    void decode(bytesConstRef) override {}
    bytesConstRef signatureData() override { return {}; }
    bcos::crypto::HashType const& signatureDataHash() override { return m_hash; }
    void setSignatureData(bytes&&) override {}
    void setSignatureData(bytes const&) override {}
    void setSignatureDataHash(bcos::crypto::HashType const&) override {}
    void setFrom(bcos::crypto::PublicPtr) override {}
    bcos::crypto::PublicPtr from() const override { return nullptr; }
    uint64_t liveTimeInMilliseconds() const override { return 0; }
    std::string toDebugString() const override { return "ThrowingPBFTMessage"; }

private:
    bcos::crypto::HashType m_hash;
};

// Expose checkSignature for testing via a subclass
class CheckSignatureEngine : public FakePBFTEngine
{
public:
    using Ptr = std::shared_ptr<CheckSignatureEngine>;
    using FakePBFTEngine::FakePBFTEngine;

    CheckResult publicCheckSignature(std::shared_ptr<PBFTBaseMessageInterface> req)
    {
        return checkSignature(req);
    }
};

BOOST_FIXTURE_TEST_SUITE(FIB136Test, TestPromptFixture)

// Before FIB-136 fix: checkSignature() called _req->verifySignature() without
// a local try-catch. When the secp256k1 backend threw on malformed input, the
// exception propagated to the consensus worker loop where it was logged as ERROR
// and processing aborted. A Byzantine peer could trigger this continuously.
//
// After fix: checkSignature() wraps verifySignature() in try-catch and converts
// exceptions to CheckResult::INVALID, preventing log spam and exception churn.
BOOST_AUTO_TEST_CASE(throwing_signature_backend_returns_invalid)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

    BlockNumber currentBlockNumber = 10;
    auto fakerMap = createFakers(cryptoSuite, 4, currentBlockNumber, 4);
    IndexType leaderIndex = 0;
    auto leaderFaker = fakerMap[leaderIndex];

    // Build an engine with the leader's config
    auto engine = std::make_shared<CheckSignatureEngine>(leaderFaker->pbftConfig());

    // Create a ThrowingPBFTMessage whose verifySignature() throws.
    // Set generatedFrom=0 so that the leader node is found in the consensus node list.
    auto throwingMsg = std::make_shared<ThrowingPBFTMessage>();
    // generatedFrom() returns 0 which maps to the first consensus node
    // (leaderIndex=0). The engine will find the node and call verifySignature().

    // FIB-136: before fix, this line would throw std::invalid_argument propagating
    // up to the worker loop. After fix, it must return INVALID without throwing.
    CheckResult result = CheckResult::VALID;
    BOOST_REQUIRE_NO_THROW(result = engine->publicCheckSignature(throwingMsg));
    BOOST_CHECK_MESSAGE(result == CheckResult::INVALID,
        "FIB-136: malformed signature that throws must be treated as INVALID, not propagated");

    for (auto& [idx, faker] : fakerMap)
    {
        faker->stop();
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
