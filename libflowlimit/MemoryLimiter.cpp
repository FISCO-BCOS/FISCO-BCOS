/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Used for Memory limit
 * @file: MemoryLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-27
 */
#include "MemoryLimiter.h"

using namespace dev::flowlimit;
void MemoryLimiter::increaseMemoryUsed(int64_t const& _memoryUsed)
{
    m_memoryUsed += _memoryUsed;
}

void MemoryLimiter::decreaseMemoryUsed(int64_t const& _memoryUsed)
{
    m_memoryUsed = (m_memoryUsed > _memoryUsed ? (m_memoryUsed - _memoryUsed) : 0);
}

bool MemoryLimiter::overMemoryLimit()
{
    return (m_memoryUsed > m_memoryLimit);
}

int64_t MemoryLimiter::getRemainingMemorySize()
{
    return (m_memoryLimit > m_memoryUsed ? (m_memoryLimit - m_memoryUsed) : 0);
}