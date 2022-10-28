#pragma once
#include "../Basic.h"
#include "../Serialize.h"
#include <bcos-task/Task.h>
#include <set>
#include <string>

namespace bcos::concepts::p2p
{

template <class Impl>
class P2P
{
public:
    task::Task<std::set<std::string>> nodeIDs() { return impl().impl_nodeIDs(); }

    task::Task<void> sendMessageByNodeID(int moduleID, auto const& nodeID,
        bcos::concepts::serialize::Serializable auto const& request,
        bcos::concepts::serialize::Serializable auto& response)
    {
        return impl().impl_sendMessageByNodeID(moduleID, nodeID, request, response);
    }

    task::Task<void> broadcastMessage(
        uint16_t type, int moduleID, bcos::concepts::serialize::Serializable auto const& request)
    {
        return impl().impl_broadcastMessage(type, moduleID, request);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::p2p