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
