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
 * @brief : complement compress and uncompress with snappy
 *
 * @file SnappyCompress.cpp
 * @author: yujiechen
 * @date 2019-03-13
 */
#include "SnappyCompress.h"

namespace bcos
{
namespace compress
{
size_t SnappyCompress::compress(bytesConstRef inputData, bytes& compressedData)
{
    size_t compressLen;
    // auto start_t = utcTimeUs();
    compressedData.resize(snappy::MaxCompressedLength(inputData.size()));
    snappy::RawCompress(
        (const char*)inputData.data(), inputData.size(), (char*)&compressedData[0], &compressLen);
    compressedData.resize(compressLen);
    /// compress failed
    if (compressLen < 1)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("SnappyCompress") << LOG_DESC("compress failed");
        return 0;
    }
#if 0
    BCOS_LOG(DEBUG) << LOG_BADGE("SnappyCompress") << LOG_DESC("Compress")
               << LOG_KV("org_len", inputData.size()) << LOG_KV("compressed_len", compressLen)
               << LOG_KV("ratio", (float)inputData.size() / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif

    return compressLen;
}

size_t SnappyCompress::uncompress(bytesConstRef compressedData, bytes& uncompressedData)
{
    size_t uncompressedLen = 0;
    // auto start_t = utcTimeUs();
    snappy::GetUncompressedLength(
        (const char*)compressedData.data(), compressedData.size(), &uncompressedLen);
    uncompressedData.resize(uncompressedLen);
    bool status = snappy::RawUncompress(
        (const char*)compressedData.data(), compressedData.size(), (char*)&uncompressedData[0]);
    /// uncompress failed
    if (!status)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("SnappyCompress") << LOG_DESC("uncompress failed");
        return 0;
    }
#if 0
    BCOS_LOG(DEBUG) << LOG_BADGE("SnappyCompress") << LOG_DESC("uncompress")
               << LOG_KV("org_len", uncompressedLen)
               << LOG_KV("compress_len", compressedData.size())
               << LOG_KV("ratio", (float)uncompressedLen / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif
    return uncompressedLen;
}
}  // namespace compress
}  // namespace bcos
