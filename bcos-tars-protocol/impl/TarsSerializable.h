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
    std::vector<std::byte> encode()
    {
        tars::TarsOutputStream<bcostars::protocol::BufferWriterStdByteVector> output;
        impl().writeTo(output);

        std::vector<std::byte> out;
        output.getByteBuffer().swap(out);

        return out;
    }

private:
    Struct& impl() { return *static_cast<Struct*>(this); }
};

}  // namespace bcostars::protocol::impl