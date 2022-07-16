#pragma once
#include "../Basic.h"

namespace bcos::concepts::p2p
{

template <class Impl>
class P2P
{
public:
    void sendMessage(RANGES::range auto buffers) requires
        ByteBuffer<std::remove_cvref_t<std::ranges::range_value_t<decltype(buffers)>>>
    {}

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};
}  // namespace bcos::concepts::p2p