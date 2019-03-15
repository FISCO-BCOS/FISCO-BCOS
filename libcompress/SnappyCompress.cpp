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
size_t SnappyCompress::compress(bytes const& inputData, bytes& compressedData)
{
    size_t compress_len;
    auto start_t = utcTimeUs();
    compressedData.resize(snappy::MaxCompressedLength(inputData.size()));
    snappy::RawCompress(
        (const char*)inputData.data(), inputData.size(), (char*)&compressedData[0], &compress_len);
    compressedData.resize(compress_len);
    LOG(DEBUG) << LOG_BADGE("SnappyCompress") << LOG_DESC("Compress")
               << LOG_KV("org_len", inputData.size())
               << LOG_KV("compressed_len", compressedData.size())
               << LOG_KV("ratio", (float)inputData.size() / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
    return compress_len;
}

size_t SnappyCompress::uncompress(bytes const& compressedData, bytes& uncompressedData)
{
    size_t uncompressed_len = 0;
    auto start_t = utcTimeUs();
    snappy::GetUncompressedLength(
        (const char*)compressedData.data(), compressedData.size(), &uncompressed_len);
    uncompressedData.resize(uncompressed_len);
    bool status = snappy::RawUncompress(
        (const char*)compressedData.data(), compressedData.size(), (char*)&uncompressedData[0]);
    if (status)
    {
        LOG(DEBUG) << LOG_BADGE("SnappyCompressio") << LOG_DESC("uncompress")
                   << LOG_KV("org_len", uncompressed_len)
                   << LOG_KV("compress_len", compressedData.size())
                   << LOG_KV("ratio", (float)uncompressed_len / (float)compressedData.size())
                   << LOG_KV("timecost", (utcTimeUs() - start_t));
    }
    else
    {
        LOG(ERROR) << LOG_BADGE("SnappyCompressio") << LOG_DESC("uncompress failed");
    }
    return uncompressed_len;
}

}  // namespace compress
}  // namespace dev
