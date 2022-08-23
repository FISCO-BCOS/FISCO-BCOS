#ifdef ENABLE_IPPCRYPTO
#pragma once

#include "Hasher.h"
#include "ippcp.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace bcos::crypto::ippcrypto
{

enum HasherType
{
    SM3_256,
    SHA3_256,
    SHA2_256,
    Keccak256,
};

template <HasherType hasherType>
class IPPCryptoHasher : public bcos::crypto::HasherBase<IPPCryptoHasher<hasherType>>
{
public:
    IPPCryptoHasher()
    {
        int hashStateSize;
        if (ippsHashGetSize_rmf(&hashStateSize) != ippStsNoErr) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"ippsHashGetSize_rmf error!"});
        }

        const IppsHashMethod* hashMethod;
        if constexpr (hasherType == SM3_256)
        {
            hashMethod = ippsHashMethod_SM3();
        }
        else if constexpr (hasherType == SHA2_256)
        {
            hashMethod = ippsHashMethod_SHA256_TT();
        }
        else
        {
            static_assert(!sizeof(*this), "Unknown Hasher Type!");
        }

        m_hashState.reset(new std::byte[hashStateSize]);
        if (ippsHashInit_rmf(hashState(), hashMethod) != ippStsNoErr) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"ippsHashInit_rmf error!"});
        }
    }

    void impl_update(std::span<std::byte const> in)
    {
        if (ippsHashUpdate_rmf(reinterpret_cast<const Ipp8u*>(in.data()),
                static_cast<int>(in.size()), hashState()) != ippStsNoErr) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"ippsHashUpdate_rmf error!"});
        }
    }

    void impl_final(std::span<std::byte> out)
    {
        if (out.size() < HASH_SIZE) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});
        }

        if (ippsHashFinal_rmf(reinterpret_cast<Ipp8u*>(out.data()), hashState()) != ippStsNoErr)
            [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"ippsHashFinal_rmf error!"});
        }
    }

    constexpr static size_t impl_hashSize() noexcept { return HASH_SIZE; }

private:
    constexpr static size_t HASH_SIZE = 32;

    IppsHashState_rmf* hashState()
    {
        return reinterpret_cast<IppsHashState_rmf*>(m_hashState.get());
    }

    std::unique_ptr<std::byte[]> m_hashState;
};

using IPPCrypto_SM3_256_Hasher = IPPCryptoHasher<SM3_256>;
using IPPCrypto_SHA2_256_Hasher = IPPCryptoHasher<SHA2_256>;

static_assert(Hasher<IPPCrypto_SM3_256_Hasher>, "Assert Hasher type");
static_assert(Hasher<IPPCrypto_SHA2_256_Hasher>, "Assert Hasher type");

}  // namespace bcos::crypto::ippcrypto

#endif