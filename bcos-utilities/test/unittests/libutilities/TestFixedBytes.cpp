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
    bcos::FixedBytes<LENGTH> bytes(std::string_view{hex}, bcos::FixedBytes<32>::FromHex);

    BOOST_CHECK_EQUAL(bytes.size(), LENGTH);
    std::string outHex;
    boost::algorithm::hex_lower(bytes.begin(), bytes.end(), std::back_inserter(outHex));
    BOOST_CHECK_NE(outHex, hex);
    auto expect = std::string(30 * 2, '0') + hex;
    BOOST_CHECK_EQUAL(outHex, expect);
}
BOOST_AUTO_TEST_SUITE_END()