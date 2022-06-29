#pragma once
#include "../Common.h"
#include "TarsStruct.h"
#include "tup/Tars.h"
#include <bcos-framework/concepts/Basic.h>
#include <ranges>
#include <vector>

namespace bcos::concepts::serialize
{
std::vector<bcos::byte> impl_encode(bcostars::protocol::impl::TarsStruct auto const& object)
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    object.writeTo(output);

    std::vector<bcos::byte> out;
    output.getByteBuffer().swap(out);

    return out;
}

void impl_decode(
    bcostars::protocol::impl::TarsStruct auto& object, bcos::concepts::ByteBuffer auto const& data)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)std::data(data), std::size(data));

    object.readFrom(input);
}
}  // namespace bcos::concepts::serialize