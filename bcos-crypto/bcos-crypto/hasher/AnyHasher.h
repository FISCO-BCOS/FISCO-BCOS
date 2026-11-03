#pragma once
#include "Hasher.h"
#include "bcos-crypto/TrivialObject.h"
#include <proxy/v4/proxy.h>
#include <range/v3/range/access.hpp>
#include <range/v3/range/primitives.hpp>
#include <span>

namespace bcos::crypto::hasher
{
class AnyHasher;

PRO_DEF_MEM_DISPATCH(MemUpdate, update);
PRO_DEF_MEM_DISPATCH(MemFinal, final);
PRO_DEF_MEM_DISPATCH(MemDigest, digest);
PRO_DEF_MEM_DISPATCH(MemClone, clone);
PRO_DEF_MEM_DISPATCH(MemHashSize, hashSize);

struct AnyHasherFacade
  : pro::facade_builder ::add_convention<MemUpdate,
        void(std::span<std::byte const>)>::add_convention<MemFinal,
        void(std::span<std::byte>)>::add_convention<MemDigest,
        void(std::span<std::byte>)>::add_convention<MemClone,
        AnyHasher() const>::add_convention<MemHashSize,
        size_t() const>::support_copy<pro::constraint_level::nontrivial>::support_relocation<pro::
            constraint_level::nothrow>::support_destruction<pro::constraint_level::nothrow>::build
{
};

// Type erasure hasher
class AnyHasher
{
private:
    pro::proxy<AnyHasherFacade> m_anyHasher;

public:
    AnyHasher() = default;
    template <SnapshotHasher H>
    AnyHasher(H hasher) : m_anyHasher(pro::make_proxy_shared<AnyHasherFacade, H>(std::move(hasher)))
    {}
    AnyHasher(AnyHasher&&) = default;
    AnyHasher& operator=(AnyHasher&&) noexcept = default;
    AnyHasher(const AnyHasher&) = default;
    AnyHasher& operator=(const AnyHasher&) = default;
    ~AnyHasher() noexcept = default;

    void update(auto const& input)
    {
        auto view = bcos::crypto::trivial::toView(std::forward<decltype(input)>(input));
        m_anyHasher->update(view);
    }

    void update(std::span<std::byte const> input);

    void final(bcos::crypto::trivial::Object auto& output)
    {
        bcos::crypto::trivial::resizeTo(output, hashSize());
        m_anyHasher->final(bcos::crypto::trivial::toView(output));
    }

    void final(std::span<std::byte> output);

    void digest(bcos::crypto::trivial::Object auto& output)
    {
        bcos::crypto::trivial::resizeTo(output, hashSize());
        m_anyHasher->digest(bcos::crypto::trivial::toView(output));
    }

    void digest(std::span<std::byte> output);

    // One-shot hash: update + final in a single call. Convenience for callers that hash a
    // self-contained buffer rather than streaming multiple chunks.
    void hash(auto const& input, auto& output)
    {
        update(input);
        final(output);
    }

    AnyHasher clone() const;
    size_t hashSize() const;
};

static_assert(Hasher<AnyHasher>, "Not a valid Hasher!");
static_assert(SnapshotHasher<AnyHasher>, "Not a valid SnapshotHasher!");
static_assert(std::is_copy_assignable_v<AnyHasher>);
static_assert(std::is_nothrow_copy_assignable_v<AnyHasher> ==
              std::is_nothrow_copy_assignable_v<pro::proxy<AnyHasherFacade>>);

// Generic one-shot hash helper: update + final in a single call. Works with any type
// satisfying the Hasher concept (AnyHasher, OpenSSLHasher<...>, mock test hashers, ...).
template <Hasher H, typename Input, typename Output>
void hash(H& hasher, Input const& input, Output& output)
{
    hasher.update(input);
    hasher.final(output);
}

}  // namespace bcos::crypto::hasher