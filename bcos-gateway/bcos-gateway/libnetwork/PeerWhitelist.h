#pragma once

#include "PeerBlackWhitelistInterface.h"


namespace bcos::gateway
{

class PeerWhitelist : public PeerBlackWhitelistInterface
{
public:
    PeerWhitelist(std::set<std::string> const& _strList, bool _enable = false)
      : PeerBlackWhitelistInterface(_strList, _enable)
    {}
    PeerWhitelist(std::set<P2PNodeID> const& _nodeList, bool _enable)
      : PeerBlackWhitelistInterface(_nodeList, _enable)
    {}

    // if not enable, all peers is in whitelist
    bool hasValueWhenDisable() const override { return true; }
};

}  // namespace bcos::gateway
