#pragma once

namespace bcos
{
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
}  // namespace bcos