#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>

namespace bcos
{

template <class Base>
struct MoveBase
{
    virtual void moveAssignTo(Base*) noexcept = 0;
    virtual void moveConstructTo(Base*) noexcept = 0;
};

template <class Derived, class Base>
struct MoveImpl : public virtual MoveBase<Base>
{
    void moveAssignTo(Base* other) noexcept override
    {
        dynamic_cast<Derived*>(other)->operator=(std::move(*dynamic_cast<Derived*>(this)));
    }
    void moveConstructTo(Base* other) noexcept override
    {
        new (other) Derived(std::move(*dynamic_cast<Derived*>(this)));
    }
};

template <class HoldType>
struct InPlace
{
};

template <class Type, std::size_t maxSize>
    requires std::derived_from<Type, MoveBase<Type>>
class AnyHolder
{
private:
    std::array<std::byte, maxSize> m_data;

    Type* get() & { return reinterpret_cast<Type*>(m_data.data()); }
    const Type* get() const& { return reinterpret_cast<const Type*>(m_data.data()); }

public:
    template <class HoldType>
        requires std::movable<HoldType> && std::derived_from<HoldType, Type> &&
                 (sizeof(HoldType) <= maxSize)
    AnyHolder(InPlace<HoldType> /*unused*/, auto&&... args)
    {
        new (m_data.data()) HoldType{std::forward<decltype(args)>(args)...};
    }
    ~AnyHolder() noexcept { get()->~Type(); }
    AnyHolder(const AnyHolder&) = delete;
    AnyHolder(AnyHolder&& other) noexcept { other.get()->moveConstructTo(get()); }
    AnyHolder& operator=(const AnyHolder&) = delete;
    AnyHolder& operator=(AnyHolder&& other) noexcept { other.get()->moveAssignTo(get()); }

    Type& operator*() & { return *get(); }
    const Type& operator*() const& { return *get(); }
    Type* operator->() & { return get(); }
    const Type* operator->() const& { return get(); }
};
}  // namespace bcos