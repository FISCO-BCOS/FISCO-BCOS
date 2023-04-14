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
 * @brief : complement compress and uncompress with zstd
 *
 * @file ZstdCompress.h
 * @author: lucasli
 * @date 2022-09-22
 */
#pragma once
#include "Common.h"
#include "zstd.h"

namespace bcos
{

class ZstdCompress
{
public:
    static bool compress(bytesConstRef inputData, bytes& compressedData, int compressionLevel);
    static bool uncompress(bytesConstRef compressedData, bytes& uncompressedData);
};

}  // namespace bcos
