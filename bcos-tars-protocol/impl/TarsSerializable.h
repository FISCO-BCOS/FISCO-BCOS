#pragma once
#include "../Common.h"
#include "tup/Tars.h"
#include <bcos-framework/concepts/Serialize.h>
#include <concepts>
#include <ranges>
#include <string>
#include <vector>

namespace bcostars::protocol::impl
{

template <class TarsStructType>
concept TarsStruct = requires(TarsStructType tarsStruct)
{
    {
        tarsStruct.className()
        } -> std::same_as<std::string>;
    {
        tarsStruct.MD5()
        } -> std::same_as<std::string>;
    tarsStruct.resetDefautlt();
};
}  // namespace bcostars::protocol::impl


namespace bcos::concepts::serialize
{
// Tars struct crtp base
std::vector<std::byte> encode(bcostars::protocol::impl::TarsStruct auto const& object)
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterStdByteVector> output;
    object.writeTo(output);

    std::vector<std::byte> out;
    output.getByteBuffer().swap(out);

    return out;
}

void decode(bcostars::protocol::impl::TarsStruct auto& object, bcos::concepts::ByteBuffer auto const& data)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)std::data(data), std::size(data));

    object.readFrom(input);
}
}  // namespace bcos::concepts::serialize