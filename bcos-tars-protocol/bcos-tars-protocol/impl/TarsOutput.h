#pragma once

#include "TarsStruct.h"
#include <ostream>

namespace std
{
ostream& operator<<(ostream& os, bcostars::protocol::impl::TarsStruct auto const& st)
{
    st.displaySimple(os);
    return os;
}
}  // namespace std