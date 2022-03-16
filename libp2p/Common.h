/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once

#include <libdevcore/FixedHash.h>
#include <libdevcore/TopicInfo.h>
#include <libnetwork/Common.h>
#include <libp2p/P2PMessage.h>

namespace dev
{
namespace p2p
{
enum DisconnectReason
{
    TOPIC_NOT_FOUND = 101
};

typedef dev::network::NodeID NodeID;
typedef dev::network::Options Options;
typedef dev::network::NetworkException NetworkException;
typedef dev::network::NodeIPEndpoint NodeIPEndpoint;

using NodeIDs = std::vector<NodeID>;

#define P2PMSG_LOG(LEVEL) LOG(LEVEL) << "[P2P][P2PMessage] "
#define SERVICE_LOG(LEVEL) LOG(LEVEL) << "[P2P][Service] "

struct P2PSessionInfo
{
    dev::network::NodeInfo nodeInfo;
    dev::network::NodeIPEndpoint nodeIPEndpoint;
    std::set<dev::TopicItem> topics;
    P2PSessionInfo(dev::network::NodeInfo const& _nodeInfo,
        dev::network::NodeIPEndpoint const& _nodeIPEndpoint,
        std::set<dev::TopicItem> const& _topics)
      : nodeInfo(_nodeInfo), nodeIPEndpoint(_nodeIPEndpoint), topics(_topics)
    {}

    NodeID const& nodeID() const { return nodeInfo.nodeID; }
};
using P2PSessionInfos = std::vector<P2PSessionInfo>;

}  // namespace p2p
}  // namespace dev
