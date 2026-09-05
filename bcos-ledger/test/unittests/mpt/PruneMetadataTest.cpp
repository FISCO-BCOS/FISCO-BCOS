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
 * @file PruneMetadataTest.cpp
 * @brief Pruning metadata key layout + RLP/BE value codec round-trips and rejection (spec §4.8)
 */
#include "TestHelpers.h"
#include <bcos-ledger/mpt/PruneMetadata.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

using namespace bcos::ledger::mpt;
using namespace bcos::ledger::mpt::test;

BOOST_AUTO_TEST_SUITE(PruneMetadataSuite)

BOOST_AUTO_TEST_CASE(RefCountRoundTrip)
{
    for (auto const& expected : {PruneRefCount{.count = 0, .pendingDeleteAt = std::nullopt},
             PruneRefCount{.count = 1, .pendingDeleteAt = std::nullopt},
             PruneRefCount{.count = 123456789, .pendingDeleteAt = std::nullopt},
             PruneRefCount{.count = 0, .pendingDeleteAt = 42},
             PruneRefCount{.count = 7, .pendingDeleteAt = 0},
             PruneRefCount{.count = UINT64_MAX, .pendingDeleteAt = UINT64_MAX}})
    {
        auto const encoded = encodeRefCount(expected);
        auto const decoded = decodeRefCount(bcos::ref(encoded));
        BOOST_CHECK(decoded == expected);
        // Re-encoding the decoded value must reproduce the bytes (canonical form).
        BOOST_CHECK(encodeRefCount(decoded) == encoded);
    }
}

BOOST_AUTO_TEST_CASE(RefCountRejectsMalformed)
{
    // Truncated RLP list header.
    BOOST_CHECK_THROW(
        decodeRefCount(bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>("\xc2"), 1)),
        MPTDecodeError);
    // A bare string, not a list.
    auto const notAList = bcos::bytes{0x83, 0x01, 0x02, 0x03};
    BOOST_CHECK_THROW(decodeRefCount(bcos::ref(notAList)), MPTDecodeError);
    // Trailing bytes after a complete [count] list.
    auto trailing = encodeRefCount(PruneRefCount{.count = 3, .pendingDeleteAt = std::nullopt});
    trailing.push_back(0x00);
    BOOST_CHECK_THROW(decodeRefCount(bcos::ref(trailing)), MPTDecodeError);
}

BOOST_AUTO_TEST_CASE(WatermarkRoundTrip)
{
    for (uint64_t const blockNumber : {uint64_t{0}, uint64_t{1}, uint64_t{0x0102030405060708},
             UINT64_MAX})
    {
        auto const encoded = encodeWatermark(blockNumber);
        BOOST_REQUIRE_EQUAL(encoded.size(), 8U);
        BOOST_CHECK_EQUAL(decodeWatermark(bcos::ref(encoded)), blockNumber);
    }
    // Big-endian byte order: the most significant byte comes first.
    auto const encoded = encodeWatermark(0x0102030405060708);
    for (size_t i = 0; i < 8; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<uint8_t>(encoded[i]), static_cast<uint8_t>(i + 1));
    }
}

BOOST_AUTO_TEST_CASE(WatermarkRejectsWrongLength)
{
    for (size_t const size : {0U, 1U, 7U, 9U})
    {
        auto const bad = bcos::bytes(size, 0);
        BOOST_CHECK_THROW(decodeWatermark(bcos::ref(bad)), MPTDecodeError);
    }
}

BOOST_AUTO_TEST_CASE(QueueKeyLayoutAndOrdering)
{
    auto const hash = makeHash(0xAB);
    auto const key = pruneQueueKey(42, hash);
    bcos::executor_v1::StateKeyView const view{key};
    BOOST_CHECK(view.m_table == kPruneQueueTable);
    BOOST_REQUIRE_EQUAL(view.m_key.size(), 8U + bcos::h256::SIZE);

    auto const [targetBlock, decodedHash] = decodeQueueKeyPart(view.m_key);
    BOOST_CHECK_EQUAL(targetBlock, 42U);
    BOOST_CHECK(decodedHash == hash);

    // Big-endian targetBlock first: lexicographic key order IS deadline order, the property the
    // deletion pass's prefix scan relies on.
    auto const earlier = pruneQueueKey(9, makeHash(0xFF));
    auto const later = pruneQueueKey(10, makeHash(0x00));
    BOOST_CHECK(earlier < later);
    // Same deadline: ordered by hash.
    BOOST_CHECK(pruneQueueKey(10, makeHash(0x00)) < pruneQueueKey(10, makeHash(0x01)));

    BOOST_CHECK_THROW(decodeQueueKeyPart(view.m_key.substr(0, 39)), MPTDecodeError);
    BOOST_CHECK_THROW(decodeQueueKeyPart(std::string_view{}), MPTDecodeError);
}

BOOST_AUTO_TEST_CASE(RefAndWatermarkKeys)
{
    auto const hash = makeHash(0x5C);
    auto const refKey = pruneRefKey(hash);
    bcos::executor_v1::StateKeyView const refView{refKey};
    BOOST_CHECK(refView.m_table == kPruneRefTable);
    BOOST_CHECK_EQUAL(refView.m_key.size(), bcos::h256::SIZE);
    BOOST_CHECK(bcos::h256(bcos::bytesConstRef(
                    reinterpret_cast<bcos::byte const*>(refView.m_key.data()), bcos::h256::SIZE)) ==
                hash);

    auto const metaKey = watermarkKey();
    bcos::executor_v1::StateKeyView const metaView{metaKey};
    BOOST_CHECK(metaView.m_table == kPruneMetaTable);
    BOOST_CHECK(metaView.m_key == kWatermarkRowKey);

    auto const seedKey = seedMarkerKey();
    bcos::executor_v1::StateKeyView const seedView{seedKey};
    BOOST_CHECK(seedView.m_table == kPruneMetaTable);
    BOOST_CHECK(seedView.m_key == kSeedMarkerRowKey);
    BOOST_CHECK(seedView.m_key != metaView.m_key);

    // The three tables are pairwise distinct and none contains a StateKey separator.
    BOOST_CHECK(kPruneRefTable != kPruneQueueTable);
    BOOST_CHECK(kPruneQueueTable != kPruneMetaTable);
    BOOST_CHECK(kPruneRefTable.find(':') == std::string_view::npos);
}

BOOST_AUTO_TEST_SUITE_END()
