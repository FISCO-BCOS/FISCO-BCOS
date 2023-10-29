#pragma once

#include "PeerBlackWhitelistInterface.h"

namespace bcos
{

namespace gateway
{

class PeerBlacklist : public PeerBlackWhitelistInterface
{
public:
    PeerBlacklist(std::set<std::string> const& _strList, bool _enable = false)
      : PeerBlackWhitelistInterface(_strList, _enable)
    {}
    PeerBlacklist(std::set<NodeID> const& _nodeList, bool _enable)
      : PeerBlackWhitelistInterface(_nodeList, _enable)
    {}

    // if not enable, all peers is not in blacklist
    bool hasValueWhenDisable() const override { return false; }
};

}  // namespace gateway

}  // namespace bcos