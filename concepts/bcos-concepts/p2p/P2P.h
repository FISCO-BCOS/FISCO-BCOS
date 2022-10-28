#pragma once
#include "../Basic.h"
#include "../Serialize.h"
#include <bcos-task/Task.h>
#include <set>
#include <string>

namespace bcos::concepts::p2p
{

template <class Impl>
class P2PBase
{
public:
    using NodeType = uint16_t;
    using ModuleID = int;

    template <RANGES::range NodeIDs>
    task::Task<NodeIDs> nodeIDs()
    {
        return impl().impl_nodeIDs();
    }

    task::Task<void> sendMessageByNodeID(ModuleID moduleID, auto const& nodeID,
        bcos::concepts::serialize::Serializable auto const& request,
        bcos::concepts::serialize::Serializable auto& response)
    {
        return impl().impl_sendMessageByNodeID(moduleID, nodeID, request, response);
    }

    task::Task<void> broadcastMessage(NodeType type, ModuleID moduleID,
        bcos::concepts::serialize::Serializable auto const& request)
    {
        return impl().impl_broadcastMessage(type, moduleID, request);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept P2P = std::derived_from<Impl, P2PBase<Impl>> ||
    std::derived_from<typename Impl::element_type, P2PBase<typename Impl::element_type>>;
}  // namespace bcos::concepts::p2p