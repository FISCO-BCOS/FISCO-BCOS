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

namespace bcos
{
template <bool T>
struct isOwner
{
};

template <typename T>
class HoldablePointer
{
public:
    HoldablePointer() noexcept = default;

    explicit HoldablePointer([[maybe_unused]] isOwner<false> owner, T* ptr) noexcept
      : m_ptr(ptr) {}
    explicit HoldablePointer([[maybe_unused]] isOwner<true> owner, T* ptr) noexcept
      : m_ptr(ptr), m_isOwner(true)
    {
        assert(ptr != nullptr && "ptr is nullptr");
    }

    HoldablePointer(const HoldablePointer&) = delete;
    HoldablePointer& operator=(const HoldablePointer&) = delete;
    HoldablePointer(HoldablePointer&& other) noexcept 
        : m_ptr(other.m_ptr), m_isOwner(other.m_isOwner)
    {
        other.m_ptr = nullptr;
        other.m_isOwner = false;
    }
    HoldablePointer& operator=(HoldablePointer&& other) noexcept
    {
        if (this != &other)
        {
            if (m_isOwner)
            {
                delete m_ptr;
            }
            m_ptr = other.m_ptr;
            m_isOwner = other.m_isOwner;
            other.m_ptr = nullptr;
            other.m_isOwner = false;
        }
        return *this;
    }
    ~HoldablePointer() noexcept
    {
        if (m_isOwner)
        {
            delete m_ptr;
        }
    }

    T* operator->() noexcept
    {
        assert(m_ptr != nullptr && "invoke operator-> on a nullptr");
        return m_ptr;
    }
    const T* operator->() const noexcept
    {
        assert(m_ptr != nullptr && "invoke operator-> on a nullptr");
        return m_ptr;
    }
    T& operator*() noexcept
    {
        assert(m_ptr != nullptr && "invoke operator* on a nullptr");
        return *m_ptr;
    }
    const T& operator*() const noexcept
    {
        assert(m_ptr != nullptr && "invoke operator* on a nullptr");
        return *m_ptr;
    }

    // raw pointer access
    T* get() noexcept { return m_ptr; }
    const T* get() const noexcept { return m_ptr; }

    // contextual conversion: non-null when holding (borrowed or owned) data
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    friend bool operator==(const HoldablePointer& lhs, const HoldablePointer& rhs) noexcept
    {
        return lhs.m_ptr == rhs.m_ptr;
    }
    friend bool operator!=(const HoldablePointer& lhs, const HoldablePointer& rhs) noexcept
    {
        return lhs.m_ptr != rhs.m_ptr;
    }
    friend bool operator==(const HoldablePointer& ptr, std::nullptr_t) noexcept
    {
        return ptr.m_ptr == nullptr;
    }
    friend bool operator==(std::nullptr_t, const HoldablePointer& ptr) noexcept
    {
        return ptr.m_ptr == nullptr;
    }
    friend bool operator!=(const HoldablePointer& ptr, std::nullptr_t) noexcept
    {
        return ptr.m_ptr != nullptr;
    }
    friend bool operator!=(std::nullptr_t, const HoldablePointer& ptr) noexcept
    {
        return ptr.m_ptr != nullptr;
    }

private:
    T* m_ptr{nullptr};
    bool m_isOwner{false};
};
}  // namespace bcos
