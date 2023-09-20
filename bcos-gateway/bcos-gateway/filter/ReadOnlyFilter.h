#pragma once
#include "Filter.h"

namespace bcos::gateway
{

struct ReadOnlyFilter
{
};

inline bool tag_invoke(tag_t<filter> /*unused*/, ReadOnlyFilter& /*unused*/,
    std::string_view groupID, std::integral auto moduleID, bcos::bytesConstRef /*unused*/)
{
    return true;
}

}  // namespace bcos::gateway