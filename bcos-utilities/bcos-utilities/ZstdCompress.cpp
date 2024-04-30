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
 * @file ZstdCompress.cpp
 * @author: lucasli
 * @date 2022-09-22
 */
#include "ZstdCompress.h"
#include "BoostLog.h"

namespace bcos
{
bool ZstdCompress::compress(bytesConstRef inputData, bytes& compressedData, int compressionLevel)
{
    // auto start_t = utcTimeUs();
    size_t const cBuffSize = ZSTD_compressBound(inputData.size());
    compressedData.resize(cBuffSize);
    auto compressedDataPtr = const_cast<void*>(static_cast<const void*>(&compressedData[0]));
    auto inputDataPtr = static_cast<const void*>(inputData.data());
    size_t const compressedSize = ZSTD_compress(
        compressedDataPtr, cBuffSize, inputDataPtr, inputData.size(), compressionLevel);
    auto code = ZSTD_isError(compressedSize);
    if (code)
    {
        // if code == 1, means compress failed
        BCOS_LOG(ERROR) << LOG_BADGE("ZstdCompress")
                        << LOG_DESC("compress failed, error code check failed")
                        << LOG_KV("code", code);
        return false;
    }
    compressedData.resize(compressedSize);
#if 0
    BCOS_LOG(DEBUG) << LOG_BADGE("ZstdCompress") << LOG_DESC("Compress")
            << LOG_KV("org_len", inputData.size()) << LOG_KV("compressed_len", compressedSize)
            << LOG_KV("ratio", (float)inputData.size() / (float)compressedData.size())
            << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif

    return true;
}

bool ZstdCompress::uncompress(bytesConstRef compressedData, bytes& uncompressedData)
{
    // auto start_t = utcTimeUs();
    size_t const cBuffSize = ZSTD_getFrameContentSize(compressedData.data(), compressedData.size());
    if (0 == cBuffSize || ZSTD_CONTENTSIZE_UNKNOWN == cBuffSize ||
        ZSTD_CONTENTSIZE_ERROR == cBuffSize)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("ZstdUncompress")
                        << LOG_DESC("compress failed, compressedData size error")
                        << LOG_KV("compressedData size", cBuffSize);
        return false;
    }

    uncompressedData.resize(cBuffSize);
    auto uncompressedDataPtr = const_cast<void*>(static_cast<const void*>(&uncompressedData[0]));
    auto compressedDataPtr = static_cast<const void*>(compressedData.data());
    size_t const uncompressSize =
        ZSTD_decompress(uncompressedDataPtr, cBuffSize, compressedDataPtr, compressedData.size());
    auto code = ZSTD_isError(uncompressSize);
    if (code)
    {
        // if code == 1, means uncompress failed
        BCOS_LOG(ERROR) << LOG_BADGE("ZstdUncompress")
                        << LOG_DESC("uncompress failed, error code check failed")
                        << LOG_KV("code", code);
        return false;
    }
    uncompressedData.resize(uncompressSize);
#if 0
    BCOS_LOG(DEBUG) << LOG_BADGE("ZstdUncompress") << LOG_DESC("uncompress")
               << LOG_KV("org_len", uncompressSize)
               << LOG_KV("compress_len", compressedData.size())
               << LOG_KV("ratio", (float)uncompressSize / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif

    return true;
}
}  // namespace bcos
