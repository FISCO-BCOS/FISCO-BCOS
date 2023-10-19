#pragma once
#include "bcos-utilities/Common.h"
#include <concepts>

namespace bcos::gateway
{

struct Filter
{
    bool operator()(auto& filter, std::string_view groupID, std::integral auto moduleID,
        bcos::bytesConstRef message) const
    {
        return tag_invoke(*this, filter, groupID, moduleID, message);
    }
};
inline constexpr Filter filter{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::gateway