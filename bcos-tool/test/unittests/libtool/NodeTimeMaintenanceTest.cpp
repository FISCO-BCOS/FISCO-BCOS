#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_CASE(testNodeTimeMaintenance_doubleNode)
{
    // create four node
    auto fixedSec1 = h256("4edbf97a0c6c3decde00ccd41f069dc30377f859fb1a9eb5759d0c9c995be244");
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto pub1 = secp256k1PriToPub(sec1);

    auto fixedSec2 = h256("52ca4bd084c9d5a309dd5d5e08e6ddb3424168ee329e9a65cdf9f20c791dbe4d");
    auto sec2 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec2.asBytes());
    auto pub2 = secp256k1PriToPub(sec2);

    auto fixedSec3 = h256("ba7699fcdc502b1ae4a7eb924ccc02db80e7d04056d2b3a114b2b2ccada4928d");
    auto sec3 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec3.asBytes());
    auto pub3 = secp256k1PriToPub(sec3);

    auto fixedSec4 = h256("21b08860aa297501e51089e01631cc915d305d18c145136a55560277ad18b283");
    auto sec4 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec4.asBytes());
    auto pub4 = secp256k1PriToPub(sec4);

    NodeTimeMaintenance nodeTimeMaintenance;
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub1, utcTime());
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub2, utcTime() + 1 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub3, utcTime() + 2 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub4, utcTime() + 3 * 60 * 1000);

    // medianTimeOffset() and getAlignedTime() derive from utcTime() sampled at
    // slightly different instants, so exact equality is racy (off-by-1ms on loaded
    // CI runners); assert within a small tolerance instead.
    constexpr int64_t tolerance = 1000;
    auto medianOffset = nodeTimeMaintenance.medianTimeOffset();
    BOOST_CHECK(medianOffset >= 90000 - tolerance && medianOffset <= 90000 + tolerance);
    auto aligned = nodeTimeMaintenance.getAlignedTime();
    auto expected = static_cast<int64_t>(utcTime()) + 90000;
    BOOST_CHECK(aligned >= expected - tolerance && aligned <= expected + tolerance);
}

BOOST_AUTO_TEST_CASE(testNodeTimeMaintenance_singlarNode)
{
    // create five node
    auto fixedSec1 = h256("4edbf97a0c6c3decde00ccd41f069dc30377f859fb1a9eb5759d0c9c995be244");
    auto sec1 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec1.asBytes());
    auto pub1 = secp256k1PriToPub(sec1);

    auto fixedSec2 = h256("52ca4bd084c9d5a309dd5d5e08e6ddb3424168ee329e9a65cdf9f20c791dbe4d");
    auto sec2 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec2.asBytes());
    auto pub2 = secp256k1PriToPub(sec2);

    auto fixedSec3 = h256("ba7699fcdc502b1ae4a7eb924ccc02db80e7d04056d2b3a114b2b2ccada4928d");
    auto sec3 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec3.asBytes());
    auto pub3 = secp256k1PriToPub(sec3);

    auto fixedSec4 = h256("21b08860aa297501e51089e01631cc915d305d18c145136a55560277ad18b283");
    auto sec4 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec4.asBytes());
    auto pub4 = secp256k1PriToPub(sec4);

    auto fixedSec5 = h256("badf6be7ea9865501aec46322b3ab2f0ddbd54e1c2c2c0502251eef85992ec1e");
    auto sec5 = std::make_shared<bcos::crypto::KeyImpl>(fixedSec5.asBytes());
    auto pub5 = secp256k1PriToPub(sec5);

    NodeTimeMaintenance nodeTimeMaintenance;
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub1, utcTime());
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub2, utcTime() + 1 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub3, utcTime() + 2 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub4, utcTime() + 3 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub5, utcTime() + 4 * 60 * 1000);

    // See doubleNode: time-derived values are sampled at different instants, so use
    // a tolerance rather than exact equality.
    constexpr int64_t tolerance = 1000;
    auto medianOffset = nodeTimeMaintenance.medianTimeOffset();
    BOOST_CHECK(medianOffset >= 120000 - tolerance && medianOffset <= 120000 + tolerance);
    auto aligned = nodeTimeMaintenance.getAlignedTime();
    auto expected = static_cast<int64_t>(utcTime()) + 120000;
    BOOST_CHECK(aligned >= expected - tolerance && aligned <= expected + tolerance);
}
}  // namespace test
}  // namespace bcos