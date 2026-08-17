#pragma once

#include "PeerBlackWhitelistInterface.h"

namespace bcos
{

namespace gateway
{

class PeerBlacklist : public PeerBlackWhitelistInterface
{
public:
    PeerBlacklist(std::set<std::string> const& _strList, bool _enable = false);
    PeerBlacklist(std::set<P2PNodeID> const& _nodeList, bool _enable);

    // if not enable, all peers is not in blacklist
    bool hasValueWhenDisable() const override;
};

}  // namespace gateway

}  // namespace bcos