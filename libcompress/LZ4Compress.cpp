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
 * @file SnappyCompress.cpp
 * @author: yujiechen
 * @date 2019-03-13
 */
#include "LZ4Compress.h"

namespace dev
{
namespace compress
{
size_t LZ4Compress::compress(
    bytesConstRef inputData, bytes& compressedData, size_t offset, bool forBroadCast)
{
    const size_t maxLen = LZ4_compressBound(inputData.size());
    compressedData.resize(maxLen + offset);

    auto start_t = utcTimeUs();
    size_t compressLen = LZ4_compress_fast((const char*)inputData.data(),
        (char*)(&compressedData[offset]), compressedData.size(), maxLen, m_speed);
    compressedData.resize(compressLen + offset);
    if (compressLen < 1)
    {
        LOG(ERROR) << LOG_BADGE("LZ4Compress") << LOG_DESC("compress failed");
        return 0;
    }
    /// update the statistic
    if (m_statistic)
    {
        m_statistic->updateCompressValue(
            inputData.size(), compressLen, (utcTimeUs() - start_t), forBroadCast);
    }
    LOG(DEBUG) << LOG_BADGE("LZ4Compress") << LOG_DESC("Compress")
               << LOG_KV("org_len", inputData.size()) << LOG_KV("compressed_len", compressLen)
               << LOG_KV("ratio", (float)inputData.size() / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));
    return compressLen;
}

size_t LZ4Compress::uncompress(bytesConstRef compressedData, bytes& uncompressedData)
{
    uncompressedData.resize(LZ4_compressBound(compressedData.size()));

    auto start_t = utcTimeUs();
    size_t uncompressLen = LZ4_decompress_fast(
        (const char*)compressedData.data(), (char*)&uncompressedData[0], compressedData.size());
    uncompressedData.resize(uncompressLen);
    /// uncompress failed
    if (uncompressLen < 1)
    {
        LOG(ERROR) << LOG_BADGE("LZ4Compress") << LOG_DESC("uncompress failed");
        return 0;
    }
    if (m_statistic)
    {
        m_statistic->updateUncompressValue(
            compressedData.size(), uncompressLen, (utcTimeUs() - start_t));
    }
    LOG(DEBUG) << LOG_BADGE("LZ4Compress") << LOG_DESC("uncompress")
               << LOG_KV("org_len", uncompressLen) << LOG_KV("compressLen", compressedData.size())
               << LOG_KV("ratio", (float)uncompressLen / (float)compressedData.size())
               << LOG_KV("timecost", (utcTimeUs() - start_t));

    return uncompressLen;
}

}  // namespace compress
}  // namespace dev
