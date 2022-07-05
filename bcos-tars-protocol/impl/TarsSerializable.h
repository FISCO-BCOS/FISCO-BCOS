#pragma once
#include "../Common.h"
#include "TarsStruct.h"
#include "tars/Block.h"
#include "tars/Transaction.h"
#include "tup/Tars.h"
#include <bcos-concepts/Basic.h>
#include <ranges>
#include <type_traits>
#include <vector>

namespace bcos::concepts::serialize
{

void impl_encode(
    bcostars::protocol::impl::TarsStruct auto const& object, bcos::concepts::ByteBuffer auto& out)
{
    using StreamType = std::conditional_t<
        std::is_signed_v<std::ranges::range_value_t<std::remove_cvref_t<decltype(out)>>>,
        tars::BufferWriter, bcostars::protocol::BufferWriterByteVector>;

    tars::TarsOutputStream<StreamType> output;
    object.writeTo(output);

    output.getByteBuffer().swap(out);
}

void impl_decode(
    bcostars::protocol::impl::TarsStruct auto& object, bcos::concepts::ByteBuffer auto const& in)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)std::data(in), std::size(in));

    object.readFrom(input);
}

}  // namespace bcos::concepts::serialize