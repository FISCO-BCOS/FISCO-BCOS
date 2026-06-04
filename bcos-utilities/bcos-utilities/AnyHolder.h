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


template <class Type, std::size_t maxSize, bool AllowMove = true, bool AllowCopy = false>
class AnyHolder
{
private:
    // 注意：m_data 需要满足 HoldType 的对齐要求。之前未加对齐限定在部分平台（mac x86）上
    // 会因为 placement new 到未对齐地址引发未定义行为（EXC_I386_GPFLT）。
    // 使用 max_align_t 作为保守上界，确保能容纳绝大多数类型的对齐需求。
    // Note: m_data must satisfy the alignment requirements of HoldType. Previously, without
    // explicit alignment on some platforms (mac x86), placement-new to an unaligned address could
    // cause undefined behavior (EXC_I386_GPFLT). Use max_align_t as a conservative upper bound to
    // cover the vast majority of alignment requirements.
    alignas(std::max_align_t) std::array<std::byte, maxSize> m_data;

    // Single VTable — one pointer (8 B) replaces up to 4 separate pointers (32 B).
    // Slots for AllowMove / AllowCopy are zero-filled when the feature is disabled.
    struct VTable
    {
        void (*destroy)(Type*) noexcept = nullptr;

        // move slots (valid only when AllowMove)
        void (*moveConstruct)(void* /*dst*/, Type* /*src*/) noexcept = nullptr;
        void (*moveAssign)(Type* /*dst*/, Type* /*src*/) noexcept = nullptr;
        std::unique_ptr<Type> (*moveToUnique)(Type* /*src*/) = nullptr;
        std::shared_ptr<Type> (*moveToShared)(Type* /*src*/) = nullptr;

        // copy slots (valid only when AllowCopy)
        void (*copyConstruct)(void* /*dst*/, const Type* /*src*/) = nullptr;
        void (*copyAssign)(Type* /*dst*/, const Type* /*src*/) = nullptr;
    };
    const VTable* m_vtable = nullptr;

    template <class HoldType>
    static const VTable* getVTableFor() noexcept
    {
        // IIFE — fills only the slots enabled by AllowMove / AllowCopy,
        // so types with deleted copy ctor don't cause hard errors.
        static const VTable vtable = []() {
            VTable vtable{};
            vtable.destroy = [](Type* ptr) noexcept {
                std::destroy_at(static_cast<HoldType*>(ptr));
            };

            if constexpr (AllowMove)
            {
                vtable.moveConstruct = [](void* dst, Type* src) noexcept {
                    new (dst) HoldType(std::move(*static_cast<HoldType*>(src)));
                };
                vtable.moveAssign = [](Type* dst, Type* src) noexcept {
                    *static_cast<HoldType*>(dst) = std::move(*static_cast<HoldType*>(src));
                };
                vtable.moveToUnique = [](Type* src) -> std::unique_ptr<Type> {
                    return std::make_unique<HoldType>(std::move(*static_cast<HoldType*>(src)));
                };
                vtable.moveToShared = [](Type* src) -> std::shared_ptr<Type> {
                    return std::make_shared<HoldType>(std::move(*static_cast<HoldType*>(src)));
                };
            }

            if constexpr (AllowCopy)
            {
                vtable.copyConstruct = [](void* dst, const Type* src) {
                    new (dst) HoldType(*static_cast<const HoldType*>(src));
                };
                vtable.copyAssign = [](Type* dst, const Type* src) {
                    *static_cast<HoldType*>(dst) = *static_cast<const HoldType*>(src);
                };
            }

            return vtable;
        }();
        return std::addressof(vtable);
    }

public:
    // ── Empty state ────────────────────────────────────────────────
    AnyHolder() = default;
    bool operator!() const noexcept { return m_vtable == nullptr; }
    explicit operator bool() const noexcept { return m_vtable != nullptr; }

    Type* get() & { return std::launder(reinterpret_cast<Type*>(m_data.data())); }
    const Type* get() const& { return std::launder(reinterpret_cast<const Type*>(m_data.data())); }

    template <class HoldType>
        requires std::movable<HoldType> && std::derived_from<HoldType, Type> &&
                 (sizeof(HoldType) <= maxSize) && (alignof(HoldType) <= alignof(std::max_align_t))
    AnyHolder(InPlace<HoldType> /*unused*/, auto&&... args) : m_vtable(getVTableFor<HoldType>())
    {
        new (m_data.data()) HoldType{std::forward<decltype(args)>(args)...};
    }
    ~AnyHolder() noexcept
    {
        if (m_vtable)
            m_vtable->destroy(get());
    }

    // ── Copy (only when AllowCopy) ─────────────────────────────────
    AnyHolder(const AnyHolder&) = delete;
    AnyHolder(const AnyHolder& other)
        requires AllowCopy
      : m_vtable(other.m_vtable)
    {
        if (m_vtable)
            m_vtable->copyConstruct(get(), other.get());
    }
    AnyHolder& operator=(const AnyHolder&) = delete;
    AnyHolder& operator=(const AnyHolder& other)
        requires AllowCopy
    {
        if (this == &other)
            return *this;
        if (m_vtable)
            m_vtable->destroy(get());
        m_vtable = other.m_vtable;
        if (m_vtable)
            m_vtable->copyConstruct(get(), other.get());
        return *this;
    }

    // ── Move (only when AllowMove) ─────────────────────────────────
    AnyHolder(AnyHolder&&) noexcept = delete;
    AnyHolder(AnyHolder&& other) noexcept
        requires AllowMove
      : m_vtable(other.m_vtable)
    {
        if (m_vtable)
            m_vtable->moveConstruct(get(), other.get());
    }
    AnyHolder& operator=(AnyHolder&&) noexcept = delete;
    AnyHolder& operator=(AnyHolder&& other) noexcept
        requires AllowMove
    {
        if (this == &other)
            return *this;
        if (m_vtable)
            m_vtable->destroy(get());
        m_vtable = other.m_vtable;
        if (m_vtable)
            m_vtable->moveConstruct(get(), other.get());
        return *this;
    }

    Type& operator*() & { return *get(); }
    const Type& operator*() const& { return *get(); }
    Type* operator->() & { return get(); }
    const Type* operator->() const& { return get(); }

    std::unique_ptr<Type> toUnique() &&
        requires AllowMove
    {
        return m_vtable->moveToUnique(get());
    }
    std::shared_ptr<Type> toShared() &&
        requires AllowMove
    {
        return m_vtable->moveToShared(get());
    }
};
}  // namespace bcos