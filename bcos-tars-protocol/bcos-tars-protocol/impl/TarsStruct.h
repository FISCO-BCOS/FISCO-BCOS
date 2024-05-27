#pragma once
#include <tup/Tars.h>
#include <concepts>

namespace bcostars::protocol::impl
{

template <class TarsStructType>
concept TarsStruct = std::derived_from<TarsStructType, tars::TarsStructBase>;

}  // namespace bcostars::protocol::impl