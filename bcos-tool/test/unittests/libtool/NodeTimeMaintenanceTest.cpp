#include <bcos-crypto/signature/key/KeyImpl.h>
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
// Small buffer added to peer times to stay above NodeTimeMaintenance::m_minInitOffset
// (60000 ms), compensating for utcTime() jitter between call sites.
static constexpr int64_t kTimeBuffer = 10;

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
    // Capture utcTime once and add a small buffer to each offset so that
    // the internal utcTime() jitter (±2ms) does not drop peer offsets below
    // NodeTimeMaintenance::m_minInitOffset (60000ms), which would cause them
    // to be clamped to 0 and skew the median.
    auto now = utcTime();
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub1, now);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub2, now + 1 * 60 * 1000 + kTimeBuffer);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub3, now + 2 * 60 * 1000 + kTimeBuffer);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub4, now + 3 * 60 * 1000 + kTimeBuffer);

    // With 4 peers the median is the average of the 2nd and 3rd offsets.
    // Offsets ≈ [0, 60010, 120010, 180010] → median ≈ 90010.
    //
    // medianTimeOffset() and getAlignedTime() derive from utcTime() sampled at slightly
    // different instants, so exact equality is racy; the windows-2025 / macos-15-intel runners
    // need a wide tolerance (they stall for hundreds of ms under load), hence 1000ms rather
    // than the tens-of-ms a local run gets away with.
    constexpr int64_t tolerance = 1000;
    auto actualMedian = nodeTimeMaintenance.medianTimeOffset();
    BOOST_CHECK_MESSAGE(actualMedian >= 90000 - tolerance && actualMedian <= 90000 + tolerance,
        "medianTimeOffset out of range: actual=" << actualMedian);
    auto expectedAligned = static_cast<int64_t>(utcTime()) + actualMedian;
    auto actualAligned = nodeTimeMaintenance.getAlignedTime();
    auto diffAligned = expectedAligned - actualAligned;
    BOOST_CHECK_MESSAGE(diffAligned >= -tolerance && diffAligned <= tolerance,
        "getAlignedTime off by " << diffAligned << "ms: expected=" << expectedAligned
                                 << " actual=" << actualAligned);
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
    // Same pattern as doubleNode test: capture once + buffer to stay above m_minInitOffset.
    auto now2 = utcTime();
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub1, now2);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub2, now2 + 1 * 60 * 1000 + kTimeBuffer);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub3, now2 + 2 * 60 * 1000 + kTimeBuffer);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub4, now2 + 3 * 60 * 1000 + kTimeBuffer);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(pub5, now2 + 4 * 60 * 1000 + kTimeBuffer);

    // With 5 peers the median is the 3rd offset ≈ 120010.
    // See doubleNode above for why the tolerance is this wide.
    constexpr int64_t tolerance = 1000;
    auto actualMedian2 = nodeTimeMaintenance.medianTimeOffset();
    BOOST_CHECK_MESSAGE(actualMedian2 >= 120000 - tolerance && actualMedian2 <= 120000 + tolerance,
        "medianTimeOffset out of range: actual=" << actualMedian2);
    auto expectedAligned2 = static_cast<int64_t>(utcTime()) + actualMedian2;
    auto actualAligned2 = nodeTimeMaintenance.getAlignedTime();
    auto diffAligned2 = expectedAligned2 - actualAligned2;
    BOOST_CHECK_MESSAGE(diffAligned2 >= -tolerance && diffAligned2 <= tolerance,
        "getAlignedTime off by " << diffAligned2 << "ms: expected=" << expectedAligned2
                                 << " actual=" << actualAligned2);
}
}  // namespace test
}  // namespace bcos