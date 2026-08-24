#include "../../../bcos-utilities/FixedBytes.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <iterator>
#include <stdexcept>

class TestFixedBytesFixture
{
public:
};

constexpr static size_t LENGTH = 32;

BOOST_FIXTURE_TEST_SUITE(TestFixedBytes, TestFixedBytesFixture)

BOOST_AUTO_TEST_CASE(fromStringView)
{
    std::string hex = "abc";
    BOOST_CHECK_THROW(
        bcos::FixedBytes<LENGTH>(std::string_view(hex), bcos::FixedBytes<32>::FromHex),
        std::invalid_argument);

    hex = "abcd";
    bcos::FixedBytes<LENGTH> rightBytes(std::string_view{hex}, bcos::FixedBytes<32>::FromHex);

    BOOST_CHECK_EQUAL(rightBytes.size(), LENGTH);
    std::string outHex;
    boost::algorithm::hex_lower(rightBytes.begin(), rightBytes.end(), std::back_inserter(outHex));
    BOOST_CHECK_NE(outHex, hex);
    auto expect = std::string(30 * 2, '0') + hex;
    BOOST_CHECK_EQUAL(outHex, expect);

    outHex.clear();
    bcos::FixedBytes<LENGTH> leftBytes(std::string_view{hex}, bcos::FixedBytes<32>::FromHex,
        bcos::FixedBytes<32>::DataAlignType::AlignLeft);
    boost::algorithm::hex_lower(leftBytes.begin(), leftBytes.end(), std::back_inserter(outHex));
    BOOST_CHECK_NE(outHex, hex);
    expect = hex + std::string(30 * 2, '0');
    BOOST_CHECK_EQUAL(outHex, expect);

    outHex.clear();
    hex = "0x50d0902a2f0da872d528fb09f6102362919e017496d62eb28f2a9bcac5ca370b";
    bcos::FixedBytes<LENGTH> prefixRightBytes(std::string_view{hex}, bcos::FixedBytes<32>::FromHex,
        bcos::FixedBytes<32>::DataAlignType::AlignLeft);
    boost::algorithm::hex_lower(
        prefixRightBytes.begin(), prefixRightBytes.end(), std::back_inserter(outHex));
    BOOST_CHECK_EQUAL("0x" + outHex, hex);

    outHex.clear();
    bcos::FixedBytes<LENGTH> prefixLeftBytes(std::string_view{hex}, bcos::FixedBytes<32>::FromHex,
        bcos::FixedBytes<32>::DataAlignType::AlignLeft);
    boost::algorithm::hex_lower(
        prefixLeftBytes.begin(), prefixLeftBytes.end(), std::back_inserter(outHex));
    BOOST_CHECK_EQUAL("0x" + outHex, hex);
}

BOOST_AUTO_TEST_CASE(fromBinaryKeepsLeading0xBytes)
{
    // Regression test: FromBinary must NOT strip a leading 0x30 0x78 (ASCII
    // "0x") from raw binary data. A 32-byte value such as a block hash that
    // happens to begin with 0x3078... must round-trip intact; previously the
    // "0x" prefix stripping (intended for hex strings only) corrupted the value
    // by dropping its first two bytes.
    std::string raw;
    const unsigned char prefix[4] = {0x30, 0x78, 0x90, 0xb5};
    raw.append(reinterpret_cast<const char*>(prefix), sizeof(prefix));
    raw.append(28, static_cast<char>(0x11));

    // Pass an explicit string_view so the string_view + FromBinary overload is
    // exercised. (A std::string argument would bind to the std::string const&
    // overload, which never strips prefixes, so the test would pass even
    // without the fix.)
    bcos::FixedBytes<LENGTH> fb(std::string_view(raw), bcos::FixedBytes<32>::FromBinary);
    auto bytes = fb.asBytes();
    BOOST_CHECK_EQUAL(bytes.size(), LENGTH);
    BOOST_CHECK_EQUAL(bytes[0], 0x30);
    BOOST_CHECK_EQUAL(bytes[1], 0x78);
    BOOST_CHECK_EQUAL(bytes[2], 0x90);
    BOOST_CHECK_EQUAL(bytes[3], 0xb5);
    BOOST_CHECK_EQUAL(bytes[4], 0x11);
    BOOST_CHECK_EQUAL(bytes[31], 0x11);
}
BOOST_AUTO_TEST_SUITE_END()