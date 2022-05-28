#pragma once
#include "../Concepts.h"
#include <boost/throw_exception.hpp>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

namespace bcos::crypto
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

    template <TrivialObject Output>
    auto calculate(TrivialObject auto&& input)
    {
        update(input);
        return final<Output>();
    }

    auto calculate(TrivialObject auto&& input)
    {
        update(input);
        return final();
    }

    auto& update(TrivialObject auto&& input)
    {
        impl().impl_update(toView(std::forward<decltype(input)>(input)));
        return *this;
    }
    void final(TrivialObject auto&& output)
    {
        impl().impl_final(toView(std::forward<decltype(output)>(output)));
    }

    template <TrivialObject Output>
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

    constexpr auto toView(TrivialObject auto&& object)
    {
        using RawType = std::remove_cvref_t<decltype(object)>;

        if constexpr (TrivialValue<RawType>)
        {
            using RawTypeWithConst = std::remove_reference_t<decltype(object)>;
            using ByteType =
                std::conditional_t<std::is_const_v<RawTypeWithConst>, std::byte const, std::byte>;
            std::span<ByteType> view{(ByteType*)&object, sizeof(object)};

            return view;
        }
        else if constexpr (TrivialRange<RawType>)
        {
            using ValueType = std::remove_reference_t<std::ranges::range_value_t<RawType>>;
            using ByteType =
                std::conditional_t<std::is_const_v<ValueType>, std::byte const, std::byte>;

            std::span<ByteType> view{(ByteType*)std::data(object),
                sizeof(std::remove_cvref_t<std::ranges::range_value_t<RawType>>) *
                    std::size(object)};

            return view;
        }
        else
        {
            static_assert(!sizeof(object), "Unsupported type!");
            return std::span<std::byte>{};
        }
    }
};

template <class Impl>
concept Hasher = std::is_base_of_v<HasherBase<Impl>, Impl>;

}  // namespace bcos::crypto