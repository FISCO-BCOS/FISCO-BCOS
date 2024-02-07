#pragma once
#include "Filter.h"
#include "bcos-framework/protocol/Protocol.h"

namespace bcos::gateway
{

struct ReadOnlyFilter
{
};

inline bool tag_invoke(tag_t<filter> /*unused*/, ReadOnlyFilter& /*unused*/,
    std::string_view groupID, std::integral auto moduleID, bcos::bytesConstRef /*unused*/)
{
    // ModuleID whitelist only allow BlockSync
    return moduleID == static_cast<decltype(moduleID)>(protocol::BlockSync);
}

}  // namespace bcos::gateway