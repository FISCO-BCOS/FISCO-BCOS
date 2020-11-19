/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once

#include <libnetwork/Common.h>
#include <libp2p/P2PMessage.h>
#include <libutilities/FixedHash.h>
#include <libutilities/TopicInfo.h>

namespace bcos
{
namespace p2p
{
enum DisconnectReason
{
    TOPIC_NOT_FOUND = 101
};

typedef bcos::network::NodeID NodeID;
typedef bcos::network::Options Options;
typedef bcos::network::NetworkException NetworkException;
typedef bcos::network::NodeIPEndpoint NodeIPEndpoint;

using NodeIDs = std::vector<NodeID>;

#define P2PMSG_LOG(LEVEL) LOG(LEVEL) << "[P2P][P2PMessage] "
#define SERVICE_LOG(LEVEL) LOG(LEVEL) << "[P2P][Service] "

struct P2PSessionInfo
{
    bcos::network::NodeInfo nodeInfo;
    bcos::network::NodeIPEndpoint nodeIPEndpoint;
    std::set<bcos::TopicItem> topics;
    P2PSessionInfo(bcos::network::NodeInfo const& _nodeInfo,
        bcos::network::NodeIPEndpoint const& _nodeIPEndpoint,
        std::set<bcos::TopicItem> const& _topics)
      : nodeInfo(_nodeInfo), nodeIPEndpoint(_nodeIPEndpoint), topics(_topics)
    {}

    NodeID const& nodeID() const { return nodeInfo.nodeID; }
};
using P2PSessionInfos = std::vector<P2PSessionInfo>;

}  // namespace p2p
}  // namespace bcos
