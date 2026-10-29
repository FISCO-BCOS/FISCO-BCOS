#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(RlpxCryptoTest)

BOOST_AUTO_TEST_CASE(keccakStreamingSnapshot)
{
    Sha3Hasher hasher;
    std::string abc = "abc";
    hasher.update(bytesConstRef(abc));
    BOOST_CHECK_EQUAL(
        toHex(hasher.hash()), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    std::string def = "def";
    hasher.update(bytesConstRef(def));
    auto continued = hasher.hash();

    Sha3Hasher reference;
    std::string abcdef = "abcdef";
    reference.update(bytesConstRef(abcdef));
    BOOST_CHECK(continued == reference.hash());
    BOOST_CHECK(keccak256(bytesConstRef(abc), bytesConstRef(def)) == continued);
}

BOOST_AUTO_TEST_CASE(keccakBlockBoundaries)
{
    std::string input(300, 'x');
    auto const* data = reinterpret_cast<byte const*>(input.data());
    Sha3Hasher streaming;
    streaming.update(bytesConstRef(data, 136));
    streaming.update(bytesConstRef(data + 136, 1));
    streaming.update(bytesConstRef(data + 137, input.size() - 137));
    BOOST_CHECK(streaming.hash() == keccak256(bytesConstRef(input)));
}

BOOST_AUTO_TEST_SUITE_END()
