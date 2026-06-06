/**
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
 * @file ObjectCounter.h
 * @author: octopuswang
 * @date 2023-01-30
 */
#pragma once

#include <atomic>
namespace bcos
{
template <typename T>
class ObjectCounter
{
public:
    ObjectCounter()
    {
        m_createdObjCount++;
        m_aliveObjCount++;
    }

    ~ObjectCounter()
    {
        m_destroyedObjCount++;
        m_aliveObjCount--;
    }

    ObjectCounter(const ObjectCounter&) = delete;
    ObjectCounter(ObjectCounter&&) = delete;

    ObjectCounter& operator=(const ObjectCounter&) = delete;
    ObjectCounter& operator=(ObjectCounter&&) = delete;

    // get m_createdObjCount&m_destroyedObjCount&m_destroyedObjCount
    static uint64_t createdObjCount() { return m_createdObjCount.load(); }
    static uint64_t destroyedObjCount() { return m_destroyedObjCount.load(); }
    static uint64_t aliveObjCount() { return m_aliveObjCount.load(); }

private:
    static std::atomic<uint64_t> m_createdObjCount;
    static std::atomic<uint64_t> m_destroyedObjCount;
    static std::atomic<uint64_t> m_aliveObjCount;
};

template <typename T>
std::atomic<uint64_t> ObjectCounter<T>::m_createdObjCount = 0;

template <typename T>
std::atomic<uint64_t> ObjectCounter<T>::m_destroyedObjCount = 0;

template <typename T>
std::atomic<uint64_t> ObjectCounter<T>::m_aliveObjCount = 0;

}  // namespace bcos
