#pragma once
#include "../Common.h"
#include "TarsStruct.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-utilities/Ranges.h>

namespace bcostars
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

void impl_decode(bcos::concepts::bytebuffer::ByteBuffer auto const& buffer,
    bcostars::protocol::impl::TarsStruct auto& out)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)RANGES::data(buffer), RANGES::size(buffer));

    out.readFrom(input);
}

}  // namespace bcostars