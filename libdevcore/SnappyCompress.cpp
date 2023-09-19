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
 * @file SnappyCompress.cpp
 * @author: yujiechen
 * @date 2019-03-13
 */
#include "SnappyCompress.h"

namespace dev
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
        LOG(ERROR) << LOG_BADGE("SnappyCompress") << LOG_DESC("compress failed");
        return 0;
    }
#if 0
    LOG(DEBUG) << LOG_BADGE("SnappyCompress") << LOG_DESC("Compress")
               << LOG_KV("org_len", inputData.size()) << LOG_KV("compressed_len", compressLen)
               << LOG_KV("ratio", (float)inputData.size() / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif

    return compressLen;
}

size_t SnappyCompress::uncompress(bytesConstRef compressedData, bytes& uncompressedData, size_t maxUncompressedLen)
{
    size_t uncompressedLen = 0;
    // auto start_t = utcTimeUs();
    snappy::GetUncompressedLength(
        (const char*)compressedData.data(), compressedData.size(), &uncompressedLen);

    if (maxUncompressedLen > 0 && uncompressedLen > maxUncompressedLen) {
        LOG(ERROR) << LOG_BADGE("SnappyCompress") << LOG_DESC("uncompress size exceeds the limit")
                   << LOG_KV("uncompressedLen", uncompressedLen) << LOG_KV("maxUncompressedLen", maxUncompressedLen);
        return 0;
    }

    uncompressedData.resize(uncompressedLen);
    bool status = snappy::RawUncompress(
        (const char*)compressedData.data(), compressedData.size(), (char*)&uncompressedData[0]);
    /// uncompress failed
    if (!status)
    {
        LOG(ERROR) << LOG_BADGE("SnappyCompress") << LOG_DESC("uncompress failed");
        return 0;
    }
#if 0
    LOG(DEBUG) << LOG_BADGE("SnappyCompress") << LOG_DESC("uncompress")
               << LOG_KV("org_len", uncompressedLen)
               << LOG_KV("compress_len", compressedData.size())
               << LOG_KV("ratio", (float)uncompressedLen / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
#endif
    return uncompressedLen;
}
}  // namespace compress
}  // namespace dev
