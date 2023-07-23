#pragma once

#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <string_view>

namespace bcos::sdk::swig
{

template <class Buffer>
bcos::bytesConstRef toBytesConstRef(Buffer const& input)
{
    return {(const bcos::byte*)input.data(), input.size()};
}

template <class Buffer>
bcos::bytes toBytes(bcos::bytesConstRef input)
{
    return {(const bcos::byte*)input.data(), (const bcos::byte*)input.data() + input.size()};
}

template <class Buffer>
h256 toH256(Buffer const& input)
{
    return {input};
}

template <class Buffer>
std::string toString(Buffer const& input)
{
    return {(const char*)input.data(), input.size()};
}

template <class Buffer>
std::string_view toStringView(Buffer input)
{
    return {(const char*)input.data(), input.size()};
}

}  // namespace bcos::sdk::swig