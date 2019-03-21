/**
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
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief : interface related to compress
 *
 * @file CompressInterface.h
 * @author: yujiechen
 * @date 2019-03-13
 */
#pragma once
#include <libdevcore/Common.h>

namespace dev
{
namespace compress
{
class CompressStatistic;
class CompressInterface
{
public:
    virtual size_t compress(bytesConstRef inputData, dev::bytes& compressedData, size_t offset = 0,
        bool forBroadcast = false) = 0;
    virtual size_t uncompress(bytesConstRef compressedData, dev::bytes& uncompressedData) = 0;
    virtual std::shared_ptr<CompressStatistic> statistic() { return nullptr; }
    virtual void createStatisticHandler() {}
};
}  // namespace compress
}  // namespace dev
