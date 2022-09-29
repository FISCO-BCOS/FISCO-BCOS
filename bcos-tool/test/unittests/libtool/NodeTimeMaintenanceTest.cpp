#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-tool/NodeTimeMaintenance.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
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
    auto node0Bytes = fromHexString(
        "6a6abf9afddd087e006019c61ef15ccdf6d1df8b51cb77abddadfd442c89283f51203e88f9988a514606ef6812"
        "21ff165c84f29532209ff0a8866548073d04b3");
    NodeIDPtr node0 = std::make_shared<KeyImpl>(*node0Bytes);
    auto node1Bytes = fromHexString(
        "b996782ddf307feef401d2316a42eebf15c52254113a99bc02adea3fadcc965ba94d472b5863e6a078d859fa14"
        "ad129e4a848d0d351cd0228f3077d1bd231684");
    NodeIDPtr node1 = std::make_shared<KeyImpl>(*node1Bytes);
    auto node2Bytes = fromHexString(
        "55da209c504014f48a77a40fb152b7a0965b4e12662deda1956887200364e1ffd53a7b9b82931232304f8037eb"
        "f5147e9c29c0ee7244bde733ff5c68ee20648e");
    NodeIDPtr node2 = std::make_shared<KeyImpl>(*node2Bytes);
    auto node3Bytes = fromHexString(
        "c21606c9ae25e82ba7f60ced0a61914d0a4c2a92eaa52dbc762e844cc73a535afbb5b65cea4134a65e44b49cdd"
        "b3a2da84703123d12be3c00609c1755c6828a3");
    NodeIDPtr node3 = std::make_shared<KeyImpl>(*node3Bytes);

    NodeTimeMaintenance nodeTimeMaintenance;
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node0, utcTime());
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node1, utcTime() + 1 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node2, utcTime() + 2 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node3, utcTime() + 3 * 60 * 1000);

    BOOST_CHECK_EQUAL(1.5 * 60 * 1000, nodeTimeMaintenance.medianTimeOffset());
    BOOST_CHECK_EQUAL(utcTime() + 1.5 * 60 * 1000, nodeTimeMaintenance.getAlignedTime());
}

BOOST_AUTO_TEST_CASE(testNodeTimeMaintenance_singlarNode)
{
    // create five node
    auto node0Bytes = fromHexString(
        "6a6abf9afddd087e006019c61ef15ccdf6d1df8b51cb77abddadfd442c89283f51203e88f9988a514606ef6812"
        "21ff165c84f29532209ff0a8866548073d04b3");
    NodeIDPtr node0 = std::make_shared<KeyImpl>(*node0Bytes);
    auto node1Bytes = fromHexString(
        "b996782ddf307feef401d2316a42eebf15c52254113a99bc02adea3fadcc965ba94d472b5863e6a078d859fa14"
        "ad129e4a848d0d351cd0228f3077d1bd231684");
    NodeIDPtr node1 = std::make_shared<KeyImpl>(*node1Bytes);
    auto node2Bytes = fromHexString(
        "55da209c504014f48a77a40fb152b7a0965b4e12662deda1956887200364e1ffd53a7b9b82931232304f8037eb"
        "f5147e9c29c0ee7244bde733ff5c68ee20648e");
    NodeIDPtr node2 = std::make_shared<KeyImpl>(*node2Bytes);
    auto node3Bytes = fromHexString(
        "c21606c9ae25e82ba7f60ced0a61914d0a4c2a92eaa52dbc762e844cc73a535afbb5b65cea4134a65e44b49cdd"
        "b3a2da84703123d12be3c00609c1755c6828a3");
    NodeIDPtr node3 = std::make_shared<KeyImpl>(*node3Bytes);
    auto node4Bytes = fromHexString(
        "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491cc1c15"
        "f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1");
    NodeIDPtr node4 = std::make_shared<KeyImpl>(*node4Bytes);

    NodeTimeMaintenance nodeTimeMaintenance;
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node0, utcTime());
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node1, utcTime() + 1 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node2, utcTime() + 2 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node3, utcTime() + 3 * 60 * 1000);
    nodeTimeMaintenance.tryToUpdatePeerTimeInfo(node4, utcTime() + 4 * 60 * 1000);

    BOOST_CHECK_EQUAL(2 * 60 * 1000, nodeTimeMaintenance.medianTimeOffset());
    BOOST_CHECK_EQUAL(utcTime() + 2 * 60 * 1000, nodeTimeMaintenance.getAlignedTime());
}
}  // namespace test
}  // namespace bcos