#pragma once
#include "../Common.h"
#include "TarsStruct.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-utilities/Ranges.h>
#include <tup/Tars.h>
#include <type_traits>
#include <vector>

namespace bcos::concepts::serialize
{

void impl_encode(bcostars::protocol::impl::TarsStruct auto const& object,
    bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    using ContainerType = typename std::remove_cvref_t<decltype(out)>;
    using WriterType = bcostars::protocol::BufferWriter<ContainerType>;

    tars::TarsOutputStream<WriterType> output;
    object.writeTo(output);

    output.getByteBuffer().swap(out);
}

void impl_decode(bcos::concepts::bytebuffer::ByteBuffer auto const& in,
    bcostars::protocol::impl::TarsStruct auto& out)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)std::data(in), RANGES::size(in));

    out.readFrom(input);
}

}  // namespace bcos::concepts::serialize