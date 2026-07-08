#pragma once

#include "TarsStruct.h"
#include <ostream>

namespace bcostars::protocol::impl
{
inline std::ostream& operator<<(std::ostream& os, TarsStruct auto const& st)
{
    st.displaySimple(os);
    return os;
}
}  // namespace bcostars::protocol::impl