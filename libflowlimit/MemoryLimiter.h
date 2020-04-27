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
 * @file: MemoryLimiter.h
 * @author: yujiechen
 * @date: 2020-04-27
 */
#pragma once
#include <libdevcore/Common.h>
#include <libethcore/Protocol.h>


namespace dev
{
namespace flowlimit
{
class MemoryLimiter
{
public:
    using Ptr = std::shared_ptr<MemoryLimiter>;
    MemoryLimiter(int64_t const& _memoryLimit) : m_memoryLimit(_memoryLimit) {}

    virtual ~MemoryLimiter() {}
    virtual void increaseMemoryUsed(int64_t const& _memoryUsed);
    virtual void decreaseMemoryUsed(int64_t const& _memoryUsed);

    bool overMemoryLimit();
    int64_t getRemainingMemorySize();

private:
    int64_t m_memoryLimit = 0;
    std::atomic<int64_t> m_memoryUsed = {0};
};
}  // namespace flowlimit
}  // namespace dev