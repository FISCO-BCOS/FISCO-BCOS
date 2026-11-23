#pragma once

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/throw_exception.hpp>
#include <cstdint>

namespace bcos::storage2
{
DERIVE_BCOS_EXCEPTION(MPTNodeStorageError);
struct StoredNode
{
    uint64_t refCount;
    uint64_t deadline;
    bcos::bytes rlp;
};

inline bcos::bytes encodeStoredNode(
    bcos::bytesConstRef rlp, uint64_t refCount, uint64_t deadline = 0)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, refCount, deadline, rlp);
    return out;
}

inline StoredNode decodeStoredNode(bcos::bytesConstRef stored)
{
    auto fail = [] {
        BOOST_THROW_EXCEPTION(
            MPTNodeStorageError{} << bcos::errinfo_comment("invalid refcounted MPT storage value"));
    };
    bcos::bytesRef cursor(const_cast<bcos::byte*>(stored.data()), stored.size());
    auto const top = bcos::codec::rlp::tryDecodeHeader(cursor);
    if (!top || !top->isList || top->payloadLength != cursor.size())
    {
        fail();
    }
    bcos::bytesRef payload(const_cast<bcos::byte*>(cursor.data()), cursor.size());
    uint64_t refCount = 0;
    uint64_t deadline = 0;
    if (auto result = bcos::codec::rlp::tryDecode(payload, refCount); !result)
    {
        fail();
    }
    if (auto result = bcos::codec::rlp::tryDecode(payload, deadline); !result)
    {
        fail();
    }
    auto const rawHeader = bcos::codec::rlp::tryDecodeHeader(payload);
    if (!rawHeader || rawHeader->isList)
    {
        fail();
    }
    auto rlp = payload.getCroppedData(0, rawHeader->payloadLength).toBytes();
    payload = payload.getCroppedData(rawHeader->payloadLength);
    if (!payload.empty())
    {
        fail();
    }
    return StoredNode{.refCount = refCount, .deadline = deadline, .rlp = std::move(rlp)};
}

inline StoredNode decodeStoredNode(std::string_view stored)
{
    return decodeStoredNode(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(stored.data()), stored.size()));
}
}  // namespace bcos::storage2
