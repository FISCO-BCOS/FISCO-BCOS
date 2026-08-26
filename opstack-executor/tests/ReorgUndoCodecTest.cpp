// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#include <opstack-executor/ReorgUndo.h>

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::opstack::ReorgUndoBlob;
using bcos::executor_v1::opstack::ReorgUndoCodec;
using bcos::executor_v1::opstack::ReorgUndoRow;

namespace
{
ReorgUndoBlob sampleBlob()
{
    ReorgUndoBlob blob;
    blob.txCount = 3;
    blob.failedCount = 1;
    bcos::storage::Entry old;
    old.set(std::string{"prev"});
    blob.rows.push_back(ReorgUndoRow{StateKey{"s_test", "k1"}, old});
    blob.rows.push_back(ReorgUndoRow{StateKey{"s_test", "k2"}, std::nullopt});
    return blob;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ReorgUndoCodecTest)

BOOST_AUTO_TEST_CASE(RoundTripKeepsAbsentAndPresent)
{
    auto const encoded = ReorgUndoCodec::encode(sampleBlob());
    auto const decoded = ReorgUndoCodec::decode(
        std::string_view{reinterpret_cast<char const*>(encoded.data()), encoded.size()});
    BOOST_CHECK_EQUAL(decoded.txCount, 3);
    BOOST_CHECK_EQUAL(decoded.failedCount, 1);
    BOOST_REQUIRE_EQUAL(decoded.rows.size(), 2);
    BOOST_CHECK(decoded.rows[0].oldValue.has_value());
    BOOST_CHECK_EQUAL(decoded.rows[0].oldValue->get(), "prev");
    BOOST_CHECK(!decoded.rows[1].oldValue.has_value());
}

BOOST_AUTO_TEST_CASE(RejectsTrailingBytes)
{
    auto encoded = ReorgUndoCodec::encode(sampleBlob());
    encoded.push_back(bcos::byte{0x00});
    BOOST_CHECK_THROW(ReorgUndoCodec::decode(std::string_view{
                          reinterpret_cast<char const*>(encoded.data()), encoded.size()}),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(RejectsInvalidHasOld)
{
    auto encoded = ReorgUndoCodec::encode(sampleBlob());
    // Flip the first row's hasOld (after version/u64/u64/u32/u32 keyLen/key).
    BOOST_REQUIRE_GE(encoded.size(), 1 + 8 + 8 + 4 + 4);
    auto keyLen = static_cast<uint32_t>(static_cast<uint8_t>(encoded[21])) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(encoded[22])) << 8) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(encoded[23])) << 16) |
                  (static_cast<uint32_t>(static_cast<uint8_t>(encoded[24])) << 24);
    auto hasOldIndex = 25 + keyLen;
    BOOST_REQUIRE_LT(hasOldIndex, encoded.size());
    encoded[hasOldIndex] = bcos::byte{2};
    BOOST_CHECK_THROW(ReorgUndoCodec::decode(std::string_view{
                          reinterpret_cast<char const*>(encoded.data()), encoded.size()}),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(RejectsCounterOverflow)
{
    bcos::bytes blob;
    blob.push_back(ReorgUndoCodec::kVersion);
    for (unsigned i = 0; i < 8; ++i)
    {
        blob.push_back(bcos::byte{0xff});
    }
    for (unsigned i = 0; i < 8; ++i)
    {
        blob.push_back(bcos::byte{0});
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        blob.push_back(bcos::byte{0});
    }
    BOOST_CHECK_THROW(ReorgUndoCodec::decode(std::string_view{
                          reinterpret_cast<char const*>(blob.data()), blob.size()}),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(RejectsHugeRowCount)
{
    bcos::bytes blob;
    blob.push_back(ReorgUndoCodec::kVersion);
    for (unsigned i = 0; i < 16; ++i)
    {
        blob.push_back(bcos::byte{0});
    }
    // rowCount = 0x01000000 (little-endian) with no row payload
    blob.push_back(bcos::byte{0});
    blob.push_back(bcos::byte{0});
    blob.push_back(bcos::byte{0});
    blob.push_back(bcos::byte{1});
    BOOST_CHECK_THROW(ReorgUndoCodec::decode(std::string_view{
                          reinterpret_cast<char const*>(blob.data()), blob.size()}),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
