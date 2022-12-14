#pragma once

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-utilities/Common.h>
#include <string_view>

namespace bcos::transaction_executor
{

template <class Impl>
concept ReadableBuffer = concepts::bytebuffer::ByteBuffer<Impl>;
template <class Impl>
concept BinaryBuffer = concepts::bytebuffer::ByteBuffer<Impl>;

template <class Impl>
concept CallParameters = requires(Impl impl)
{
    // clang-format off
    { impl.senderAddress() } -> ReadableBuffer;                    // common field, readable format
    { impl.codeAddress() } -> ReadableBuffer;                      // common field, readable format
    { impl.receiveAddress() } -> ReadableBuffer;                   // common field, readable format
    { impl.origin() } -> ReadableBuffer;                           // common field, readable format
    { impl.gasLeft() } -> std::integral;                           // common field
    { impl.data() } -> BinaryBuffer;                               // common field, transaction data, binary format
    { impl.abi() } -> BinaryBuffer;                                // common field, contract abi, json format

    { impl.status() } -> std::integral;
    { impl.evmStatus() } -> std::integral;
    { impl.staticCall() } -> std::integral;
    { impl.create() } -> std::integral;
    { impl.internalCreate() } -> std::integral;
    // clang-format on
};

template <class Impl>
concept ResponseParameters = requires(Impl impl)
{
    // clang-format off
    CallParameters<Impl>;
    { impl.message() } -> ReadableBuffer;
    { impl.logEntries() } -> RANGES::range; 
    { impl.createSalt() } -> std::same_as<std::optional<u256>>;
    { impl.newEVMContractAddress() } -> ReadableBuffer;
    // clang-format on
};

}  // namespace bcos::transaction_executor