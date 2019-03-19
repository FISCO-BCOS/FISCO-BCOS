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
 * @brief : statistic class for Compress module
 *
 * @file CompressStatistic.h
 * @author: yujiechen
 * @date 2019-03-13
 */
#pragma once
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>

namespace dev
{
namespace compress
{
class CompressStatistic
{
public:
    void setSealerSize(uint64_t sealerSize)
    {
        WriteGuard l(x_sealerSize);
        m_sealerSize = sealerSize - 1;
    }

    uint64_t getBroadcastSize()
    {
        ReadGuard l(x_sealerSize);
        return m_sealerSize;
    }
    void updateCompressValue(uint64_t const& orgDataSize, uint64_t const& compressedDataSize,
        uint64_t compressTime, bool forBroadcast)
    {
        WriteGuard l(x_compress);
        if (forBroadcast)
        {
            m_sendDataSize += (compressedDataSize * getBroadcastSize());
            m_savedSendData += (compressedDataSize * getBroadcastSize());
        }
        else
        {
            m_sendDataSize += compressedDataSize;
            m_savedSendData += compressedDataSize;
        }
        m_compressOrgDataSize += orgDataSize;
        m_compressedDataSize += compressedDataSize;
        m_compressTime += compressTime;
    }

    void updateUncompressValue(
        uint64_t const& orgDataSize, uint64_t const& uncompressedDataSize, uint64_t uncompressTime)
    {
        WriteGuard l(x_uncompress);
        m_uncompressOrgDataSize += orgDataSize;
        m_uncompressedDataSize += uncompressedDataSize;
        m_uncompressTime += uncompressTime;
    }

    uint64_t orgCompressDataSize()
    {
        dev::ReadGuard l(x_compress);
        return m_compressOrgDataSize;
    }

    uint64_t orgUncompressDataSize()
    {
        dev::ReadGuard l(x_uncompress);
        return m_uncompressOrgDataSize;
    }

    uint64_t compressDataSize()
    {
        dev::ReadGuard l(x_compress);
        return m_compressedDataSize;
    }

    uint64_t uncompressDataSize()
    {
        dev::ReadGuard l(x_compress);
        return m_uncompressedDataSize;
    }

    uint64_t compressTime()
    {
        dev::ReadGuard l(x_compress);
        return m_compressTime;
    }
    uint64_t uncompressTime()
    {
        dev::ReadGuard l(x_uncompress);
        return m_uncompressTime;
    }

    uint64_t sendDataSize()
    {
        dev::ReadGuard l(x_compress);
        return m_sendDataSize;
    }

    uint64_t recvDataSize()
    {
        dev::ReadGuard l(x_uncompress);
        return m_uncompressOrgDataSize;
    }

    uint64_t savedSendDataSize()
    {
        dev::ReadGuard l(x_compress);
        return m_savedSendData;
    }

private:
    uint64_t m_compressOrgDataSize = 0;
    uint64_t m_compressedDataSize = 0;

    uint64_t m_uncompressOrgDataSize = 0;
    uint64_t m_uncompressedDataSize = 0;

    uint64_t m_sendDataSize = 0;

    uint64_t m_savedSendData = 0;
    uint64_t m_savedRecvData = 0;

    uint64_t m_compressTime = 0;
    uint64_t m_uncompressTime = 0;

    mutable dev::SharedMutex x_sealerSize;
    uint64_t m_sealerSize = 1;

    mutable dev::SharedMutex x_compress;
    mutable dev::SharedMutex x_uncompress;
};
}  // namespace compress
}  // namespace dev
