#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>

namespace bcos
{
template <class HoldType>
struct InPlace
{
};


template <class Type, std::size_t maxSize>
class AnyHolder
{
private:
    std::array<std::byte, maxSize> m_data;
    struct MoveOperators
    {
        void (*destroy)(Type*) noexcept;
        void (*moveConstruct)(void* /*dst*/, Type* /*src*/) noexcept;
        void (*moveAssign)(Type* /*dst*/, Type* /*src*/) noexcept;
        std::unique_ptr<Type> (*moveToUnique)(Type* /*src*/);
        std::shared_ptr<Type> (*moveToShared)(Type* /*src*/);
    };
    const MoveOperators* m_moveOperators;

    template <class HoldType>
    static const MoveOperators* getMoveOperatorsFor() noexcept
    {
        static const MoveOperators moveOperators{// destroy
            [](Type* ptrValue) noexcept { std::destroy_at(static_cast<HoldType*>(ptrValue)); },
            // moveConstruct
            [](void* dst, Type* src) noexcept {
                new (dst) HoldType(std::move(*static_cast<HoldType*>(src)));
            },
            // moveAssign
            [](Type* dst, Type* src) noexcept {
                *static_cast<HoldType*>(dst) = std::move(*static_cast<HoldType*>(src));
            },
            // moveToUnique
            [](Type* src) -> std::unique_ptr<Type> {
                auto uniqueDerived =
                    std::make_unique<HoldType>(std::move(*static_cast<HoldType*>(src)));
                return std::unique_ptr<Type>(std::move(uniqueDerived));
            },
            // moveToShared
            [](Type* src) -> std::shared_ptr<Type> {
                std::shared_ptr<HoldType> sharedDerived =
                    std::make_shared<HoldType>(std::move(*static_cast<HoldType*>(src)));
                return std::static_pointer_cast<Type>(std::move(sharedDerived));
            }};
        return std::addressof(moveOperators);
    }

public:
    Type* get() & { return reinterpret_cast<Type*>(m_data.data()); }
    const Type* get() const& { return reinterpret_cast<const Type*>(m_data.data()); }

    template <class HoldType>
        requires std::movable<HoldType> && std::derived_from<HoldType, Type> &&
                 (sizeof(HoldType) <= maxSize)
    AnyHolder(InPlace<HoldType> /*unused*/, auto&&... args)
      : m_moveOperators(getMoveOperatorsFor<HoldType>())
    {
        new (m_data.data()) HoldType{std::forward<decltype(args)>(args)...};
    }
    ~AnyHolder() noexcept { m_moveOperators->destroy(get()); }
    AnyHolder(const AnyHolder&) = delete;
    AnyHolder(AnyHolder&& other) noexcept : m_moveOperators(other.m_moveOperators)
    {
        m_moveOperators->moveConstruct(get(), other.get());
    }
    AnyHolder& operator=(const AnyHolder&) = delete;
    AnyHolder& operator=(AnyHolder&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        if (m_moveOperators == other.m_moveOperators)
        {
            // Same concrete type, do move-assign
            m_moveOperators->moveAssign(get(), other.get());
        }
        else
        {
            // Different type (or uninitialized), destroy and move-construct new one
            m_moveOperators->destroy(get());
            m_moveOperators = other.m_moveOperators;
            m_moveOperators->moveConstruct(get(), other.get());
        }
        return *this;
    }

    Type& operator*() & { return *get(); }
    const Type& operator*() const& { return *get(); }
    Type* operator->() & { return get(); }
    const Type* operator->() const& { return get(); }

    std::unique_ptr<Type> toUnique() && { return m_moveOperators->moveToUnique(get()); }
    std::shared_ptr<Type> toShared() && { return m_moveOperators->moveToShared(get()); }
};
}  // namespace bcos