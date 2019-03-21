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
 * @brief : complement compress and uncompress with lz4
 *
 * @file LZ4Compress.h
 * @author: yujiechen
 * @date 2019-03-13
 */
#pragma once
#include "CompressInterface.h"
#include "lz4.h"
#include <libdevcore/easylog.h>

namespace dev
{
namespace compress
{
class LZ4Compress : public CompressInterface
{
public:
    LZ4Compress() {}
    size_t compress(bytesConstRef inputData, bytes& compressedData, size_t offset = 0,
        bool forBroadCast = false) override;
    size_t uncompress(bytesConstRef compressedData, bytes& uncompressedData) override;

private:
    int m_speed = 1;
};
}  // namespace compress
}  // namespace dev
