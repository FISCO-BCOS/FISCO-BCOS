#pragma once
#include "../Common.h"
#include "tup/Tars.h"
#include <concepts>
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

// Tars struct crtp base
template <TarsStruct Struct>
class TarsSerializable
{
public:
    std::vector<std::byte> encode() const
    {
        tars::TarsOutputStream<bcostars::protocol::BufferWriterStdByteVector> output;
        impl().writeTo(output);

        std::vector<std::byte> out;
        output.getByteBuffer().swap(out);

        return out;
    }

    void decode(std::vector<std::byte const> const& data)
    {
        tars::TarsInputStream<tars::BufferReader> input;
        input.setBuffer((const char*)data.data(), data.size());

        impl()->readFrom(input);
    }

private:
    Struct& impl() { return *static_cast<Struct*>(this); }
};

}  // namespace bcostars::protocol::impl