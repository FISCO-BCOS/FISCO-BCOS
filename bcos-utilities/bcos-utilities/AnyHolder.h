#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>

namespace bcos
{

template <class T>
concept HasMoveMethod = requires(T* from, T* toObj) {
    { from->moveAssign(toObj) };
    { from->moveConstruct(toObj) };
};

template <class Base>
struct MoveBase
{
    virtual void moveAssign(Base*) noexcept = 0;
    virtual void moveConstruct(Base*) noexcept = 0;
};

template <class Derived, class Base>
struct MoveImpl : public virtual MoveBase<Base>
{
    void moveAssign(Base* toObj) noexcept override
    {
        static_cast<Derived*>(toObj)->operator=(std::move(*dynamic_cast<Derived*>(this)));
    }
    void moveConstruct(Base* toObj) noexcept override
    {
        new (toObj) Derived(std::move(*dynamic_cast<Derived*>(this)));
    }
};

template <class HoldType>
struct InPlace
{
    using Type = HoldType;
};

template <class Type, std::size_t maxSize>
    requires HasMoveMethod<Type>
class AnyHolder
{
private:
    std::array<std::byte, maxSize> m_data;

    Type* convertTo() & { return reinterpret_cast<Type*>(m_data.data()); }
    const Type* convertTo() const& { return reinterpret_cast<const Type*>(m_data.data()); }

public:
    template <class HoldType>
        requires std::movable<HoldType> && std::derived_from<HoldType, Type> &&
                 (sizeof(HoldType) <= maxSize)
    AnyHolder(InPlace<HoldType> /*unused*/, auto&&... args)
    {
        new (m_data.data()) HoldType(std::forward<decltype(args)>(args)...);
    }
    ~AnyHolder() noexcept { convertTo()->~Type(); }
    AnyHolder(const AnyHolder&) = delete;
    AnyHolder(AnyHolder&& toObj) noexcept { toObj.convertTo()->moveConstruct(convertTo()); }
    AnyHolder& operator=(const AnyHolder&) = delete;
    AnyHolder& operator=(AnyHolder&& toObj) noexcept { toObj.convertTo()->moveAssign(convertTo()); }

    Type& get() & { return *convertTo(); };
    const Type& get() const& { return *convertTo(); };
    Type& operator*() & { return get(); }
    const Type& operator*() const& { return get(); }
    Type* operator->() & { return convertTo(); }
    const Type* operator->() const& { return convertTo(); }
};
}  // namespace bcos