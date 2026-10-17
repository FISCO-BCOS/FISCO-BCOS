/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file PrecompiledImpl.cpp
 */

#include "PrecompiledImpl.h"

bcos::executor_v1::Precompiled::Precompiled(decltype(m_precompiled) precompiled)
  : m_precompiled(std::move(precompiled))
{}

bcos::executor_v1::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, ledger::Features::Flag flag)
  : m_precompiled(std::move(precompiled)), m_flag(flag)
{}

bcos::executor_v1::Precompiled::Precompiled(
    decltype(m_precompiled) precompiled, size_t size)
  : m_precompiled(std::move(precompiled)), m_size(size)
{}

size_t bcos::executor_v1::size(Precompiled const& precompiled)
{
    return precompiled.m_size;
}

std::optional<bcos::ledger::Features::Flag> bcos::executor_v1::featureFlag(
    Precompiled const& precompiled)
{
    return precompiled.m_flag;
}
