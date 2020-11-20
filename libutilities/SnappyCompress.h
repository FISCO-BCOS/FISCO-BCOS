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
 * @brief : complement compress and uncompress with snappy
 *
 * @file SnappyCompress.h
 * @author: yujiechen
 * @date 2019-03-13
 */
#pragma once
#include "Common.h"
#include "snappy.h"

namespace bcos
{
namespace compress
{
class SnappyCompress
{
public:
    static size_t compress(bytesConstRef inputData, bytes& compressedData);
    static size_t uncompress(bytesConstRef compressedData, bytes& uncompressedData);
};
}  // namespace compress
}  // namespace bcos
