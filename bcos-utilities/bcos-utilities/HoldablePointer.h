/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file HoldablePointer.h
 * @author stanwu
 * @date 2026-08-06
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace bcos
{
template <bool T>
struct isOwner
{
};

template <typename T>
    requires(alignof(T) >= 2)
class HoldablePointer
{
public:
    HoldablePointer() noexcept = default;

    explicit HoldablePointer([[maybe_unused]] isOwner<false> owner, T* ptr) noexcept
      : m_ptr(reinterpret_cast<uintptr_t>(ptr))
    {
        assert(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0 && "ptr is not aligned");
    }
    explicit HoldablePointer([[maybe_unused]] isOwner<true> owner, T* ptr) noexcept
      : m_ptr(reinterpret_cast<uintptr_t>(ptr) | 1)
    {
        assert(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0 && "ptr is not aligned");
    }

    HoldablePointer(const HoldablePointer&) = delete;
    HoldablePointer& operator=(const HoldablePointer&) = delete;
    HoldablePointer(HoldablePointer&& other) noexcept : m_ptr(other.m_ptr)
    {
        other.m_ptr = 0;
    }
    HoldablePointer& operator=(HoldablePointer&& other) noexcept
    {
        if (this != &other)
        {
            if (m_ptr & 1)
            {
                delete reinterpret_cast<T*>(m_ptr & ~1);
            }
            m_ptr = other.m_ptr;
            other.m_ptr = 0;
        }
        return *this;
    }
    ~HoldablePointer() noexcept
    {
        if (m_ptr & 1)
        {
            delete reinterpret_cast<T*>(m_ptr & ~1);
        }
    }

    T* operator->() noexcept
    {
        assert(m_ptr != 0 && "invoke operator-> on a nullptr");
        return reinterpret_cast<T*>(m_ptr & ~1);
    }
    const T* operator->() const noexcept
    {
        assert(m_ptr != 0 && "invoke operator-> on a nullptr");
        return reinterpret_cast<const T*>(m_ptr & ~1);
    }
    T& operator*() noexcept
    {
        assert(m_ptr != 0 && "invoke operator* on a nullptr");
        return *reinterpret_cast<T*>(m_ptr & ~1);
    }
    const T& operator*() const noexcept
    {
        assert(m_ptr != 0 && "invoke operator* on a nullptr");
        return *reinterpret_cast<const T*>(m_ptr & ~1);
    }

    // raw pointer access
    T* get() noexcept { return reinterpret_cast<T*>(m_ptr & ~1); }
    const T* get() const noexcept { return reinterpret_cast<const T*>(m_ptr & ~1); }

    // contextual conversion: non-null when holding (borrowed or owned) data
    explicit operator bool() const noexcept { return m_ptr != 0; }

    friend bool operator==(const HoldablePointer& lhs, const HoldablePointer& rhs) noexcept
    {
        return lhs.get() == rhs.get();
    }
    friend bool operator!=(const HoldablePointer& lhs, const HoldablePointer& rhs) noexcept
    {
        return !(lhs == rhs);
    }
    friend bool operator==(const HoldablePointer& ptr, std::nullptr_t) noexcept
    {
        return ptr.m_ptr == 0;
    }
    friend bool operator==(std::nullptr_t, const HoldablePointer& ptr) noexcept
    {
        return ptr.m_ptr == 0;
    }
    friend bool operator!=(const HoldablePointer& ptr, std::nullptr_t) noexcept
    {
        return ptr.m_ptr != 0;
    }
    friend bool operator!=(std::nullptr_t, const HoldablePointer& ptr) noexcept
    {
        return ptr.m_ptr != 0;
    }

private:
    uintptr_t m_ptr{0};
};
}  // namespace bcos
