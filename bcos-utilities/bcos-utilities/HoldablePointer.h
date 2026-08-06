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

#include <cstdint>
#include <cassert>

namespace bcos
{

template <bool T>
struct isOwner{
};

template <typename T>
    requires (alignof(T) >= 2)
class HoldablePointer
{
public:
    HoldablePointer() noexcept = default;

    explicit HoldablePointer(isOwner<false>, T* ptr) noexcept : m_ptr(reinterpret_cast<uintptr_t>(ptr)) 
    {
        assert(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0 && "ptr is not aligned");
    }
    explicit HoldablePointer(isOwner<true>, T* ptr) noexcept : m_ptr(reinterpret_cast<uintptr_t>(ptr) | 1) 
    {
        assert(reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0 && "ptr is not aligned");
    }

    HoldablePointer(const HoldablePointer& other)  = delete;
    HoldablePointer& operator=(const HoldablePointer& other) = delete;
    HoldablePointer(HoldablePointer&& other) noexcept : m_ptr(other.m_ptr)
    {
        other.m_ptr = 0;
    }
    HoldablePointer& operator=(HoldablePointer&& other) noexcept
    {
        m_ptr = other.m_ptr;
        other.m_ptr = 0;
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

private:

    uintptr_t m_ptr{0};
};
}