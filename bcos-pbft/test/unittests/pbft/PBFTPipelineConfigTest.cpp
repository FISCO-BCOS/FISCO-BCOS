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
 * @brief Tests for PBFTPipeline runtime config: enable/disable switch and
 *        default window sizes.
 * @file PBFTPipelineConfigTest.cpp
 * @author: kyonRay
 * @date 2026-05-12
 */
#include "bcos-pbft/pbft/utilities/PBFTPipeline.h"
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{

class PipelineConfigStubMsg : public PBFTBaseMessageInterface
{
public:
    PipelineConfigStubMsg(PacketType type, int64_t index, KeyInterface::Ptr from)
      : m_type(type), m_index(index), m_from(std::move(from))
    {}
    PacketType packetType() const override { return m_type; }
    int64_t index() const override { return m_index; }
    bcos::crypto::HashType const& hash() const override { return m_hash; }
    ViewType view() const override { return 0; }
    IndexType generatedFrom() const override { return 0; }
    int64_t timestamp() const override { return 0; }
    int32_t version() const override { return 0; }
    void setIndex(int64_t newIndex) override { m_index = newIndex; }
    void setTimestamp(int64_t) override {}
    void setVersion(int32_t) override {}
    void setView(ViewType) override {}
    void setGeneratedFrom(IndexType) override {}
    void setHash(bcos::crypto::HashType const& newHash) override { m_hash = newHash; }
    void setPacketType(PacketType newType) override { m_type = newType; }
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
    bool verifySignature(bcos::crypto::CryptoSuite::Ptr, bcos::crypto::PublicPtr) override
    {
        return true;
    }
    void setFrom(bcos::crypto::PublicPtr from) override { m_from = std::move(from); }
    bcos::crypto::PublicPtr from() const override { return m_from; }
    uint64_t liveTimeInMilliseconds() const override { return 0; }
    std::string toDebugString() const override { return "PipelineConfigStubMsg"; }

private:
    PacketType m_type;
    int64_t m_index;
    KeyInterface::Ptr m_from;
    HashType m_hash;
};

static KeyInterface::Ptr makePeer(std::string const& tag)
{
    return std::make_shared<KeyImpl>(bytes(tag.begin(), tag.end()));
}

BOOST_FIXTURE_TEST_SUITE(PBFTPipelineConfigTest, TestPromptFixture)

// Disabled pipeline must admit every message regardless of stage filters.
BOOST_AUTO_TEST_CASE(disabled_pipeline_admits_everything)
{
    PBFTPipeline::Config cfg;
    cfg.enabled = false;
    PBFTPipeline pipeline(cfg);
    auto peer = makePeer("peerD");

    auto stale = std::make_shared<PipelineConfigStubMsg>(PacketType::PrePreparePacket, 5, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(stale, /*lastApplied=*/100, /*maxFutureIndex=*/200),
        "disabled pipeline must admit even stale msgs");

    auto far = std::make_shared<PipelineConfigStubMsg>(PacketType::PrePreparePacket, 99999, peer);
    BOOST_CHECK_MESSAGE(pipeline.admit(far, /*lastApplied=*/100, /*maxFutureIndex=*/200),
        "disabled pipeline must admit even far-future msgs");
}

// Enabled pipeline still applies Stage 1/1b filters (sanity check).
BOOST_AUTO_TEST_CASE(enabled_pipeline_still_drops_stale_and_future)
{
    PBFTPipeline::Config cfg;
    cfg.enabled = true;
    PBFTPipeline pipeline(cfg);
    auto peer = makePeer("peerE");

    auto stale = std::make_shared<PipelineConfigStubMsg>(PacketType::PrePreparePacket, 5, peer);
    BOOST_CHECK(!pipeline.admit(stale, /*lastApplied=*/100, /*maxFutureIndex=*/200));

    auto far = std::make_shared<PipelineConfigStubMsg>(PacketType::PrePreparePacket, 99999, peer);
    BOOST_CHECK(!pipeline.admit(far, /*lastApplied=*/100, /*maxFutureIndex=*/200));
}

// Default-constructed Config must have enabled = true and the historical
// hard-coded window sizes, so existing two-arg PBFTPipeline() callers (in
// FIB145/146 tests) keep their original semantics.
BOOST_AUTO_TEST_CASE(default_config_enabled_and_historical_windows)
{
    PBFTPipeline::Config cfg;
    BOOST_CHECK_EQUAL(cfg.enabled, true);
    BOOST_CHECK_EQUAL(cfg.perPeerCapacity, 64U);
    BOOST_CHECK_EQUAL(cfg.lruCapacity, 256U);
    BOOST_CHECK_EQUAL(cfg.maxPeers, 1024U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
