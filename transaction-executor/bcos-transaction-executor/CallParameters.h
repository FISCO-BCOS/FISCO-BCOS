#pragma once

#include <bcos-concepts/ByteBuffer.h>
#include <string_view>

namespace bcos::transaction_executor
{

template <class Impl>
concept CallParameters = requires(Impl impl)
{
    // clang-format off
    { impl.senderAddress() } -> concepts::bytebuffer::ByteBuffer;  // common field, readable format
    { impl.codeAddress() } -> concepts::bytebuffer::ByteBuffer;    // common field, readable format
    { impl.receiveAddress() } -> concepts::bytebuffer::ByteBuffer; // common field, readable format
    { impl.origin() } -> concepts::bytebuffer::ByteBuffer;         // common field, readable format
    { impl.gasLeft() } -> std::integral;                           // common field
    { impl.data() } -> concepts::bytebuffer::ByteBuffer;           // common field, transaction data, binary format
    { impl.abi() } -> concepts::bytebuffer::ByteBuffer;            // common field, contract abi, json format

    { impl.message() } -> concepts::bytebuffer::ByteBuffer;        // by response, readable format

    // clang-format on
};

}  // namespace bcos::transaction_executor