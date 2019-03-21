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
#include <libdevcore/CommonData.h>

namespace dev
{
namespace compress
{
size_t LZ4Compress::compress(
    bytesConstRef inputData, bytes& compressedData, size_t offset, bool forBroadCast)
{
    auto start_t = utcTimeUs();
    /// save the origin data len
    compressedData.resize(offset);
    saveOriginDataLen(inputData, compressedData);

    /// compress
    const size_t maxLen = LZ4_compressBound(inputData.size());
    auto compressOffset = offset + m_dataLenFields;
    compressedData.resize(maxLen + compressOffset);

    size_t compressLen = LZ4_compress_fast((const char*)inputData.data(),
        (char*)(&compressedData[compressOffset]), inputData.size(), maxLen, m_speed);

    compressedData.resize(compressLen + compressOffset);
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

void LZ4Compress::saveOriginDataLen(bytesConstRef inputData, bytes& compressedData)
{
    /// append the origin data length
    bytes originDataSizeBytes(m_dataLenFields);
    toBigEndian(inputData.size(), originDataSizeBytes);
    /// copy the len
    compressedData.insert(
        compressedData.end(), originDataSizeBytes.begin(), originDataSizeBytes.end());
}

size_t LZ4Compress::loadOriginDataLen(bytesConstRef inputData)
{
    bytes originDataSizeBytes(inputData.begin(), inputData.begin() + m_dataLenFields);
    return fromBigEndian<size_t>(originDataSizeBytes);
}

size_t LZ4Compress::uncompress(bytesConstRef compressedData, bytes& uncompressedData)
{
    auto start_t = utcTimeUs();

    /// obtain the origin data size
    size_t uncompressLen = loadOriginDataLen(compressedData);
    uncompressedData.resize(uncompressLen);

    /// uncompress
    int ret = LZ4_decompress_fast((const char*)(&compressedData.data()[m_dataLenFields]),
        (char*)&uncompressedData[0], uncompressLen);
    /// uncompress failed
    if (ret < 1 || uncompressLen < 1)
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
