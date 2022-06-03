#pragma once
#include "../Concepts.h"
#include <boost/throw_exception.hpp>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

// Hasher CRTP base
template <class Impl>
class HasherBase
{
public:
    HasherBase() = default;
    HasherBase(const HasherBase&) = default;
    HasherBase(HasherBase&&) = default;
    HasherBase& operator=(const HasherBase&) = default;
    HasherBase& operator=(HasherBase&&) = default;
    virtual ~HasherBase() = default;

    template <trivial::Object Output>
    auto calculate(trivial::Object auto&& input)
    {
        update(input);
        return final<Output>();
    }

    auto calculate(trivial::Object auto&& input)
    {
        update(input);
        return final();
    }

    auto& update(trivial::Object auto&& input)
    {
        impl().impl_update(trivial::toView(std::forward<decltype(input)>(input)));
        return *this;
    }
    void final(trivial::Object auto&& output)
    {
        impl().impl_final(trivial::toView(std::forward<decltype(output)>(output)));
    }

    template <trivial::Object Output>
    auto final()
    {
        Output output;
        final(output);
        return output;
    }

    auto final()
    {
        std::array<std::byte, Impl::impl_hashSize()> output;
        final(output);
        return output;
    }

private:
    constexpr Impl& impl() { return *static_cast<Impl*>(this); }
};

template <class Impl>
concept Hasher = std::is_base_of_v<HasherBase<Impl>, Impl>;

}  // namespace bcos::crypto::hasher