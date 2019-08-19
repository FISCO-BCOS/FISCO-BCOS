/*
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
 */
/** @file BinLogHandler.h
 *  @author chaychen
 *  @date 20190802
 */
#pragma once

#include "Common.h"
#include "Table.h"
#include <boost/asio.hpp>
#include <fstream>

namespace dev
{
namespace storage
{
#ifndef HTONLL
#define HTONLL(x)            \
    ((1 == htonl(1)) ? (x) : \
                       (((uint64_t)htonl((x)&0xFFFFFFFFUL)) << 32) | htonl((uint32_t)((x) >> 32)))
#endif
#ifndef NTOHLL
#define NTOHLL(x)            \
    ((1 == ntohl(1)) ? (x) : \
                       (((uint64_t)ntohl((x)&0xFFFFFFFFUL)) << 32) | ntohl((uint32_t)((x) >> 32)))
#endif

class BinLogHandler
{
public:
    BinLogHandler(const std::string& path);
    virtual ~BinLogHandler();

    /// set the path of binlog storage
    void setBinLogStoragePath(const std::string& path);

    /// write block data to binlog before commit data in cachedStorage
    /// @return true : write binlog successfully
    /// @return false : something went wrong in the writing process
    bool writeBlocktoBinLog(int64_t num, const std::vector<TableData::Ptr>& datas);

    /// get block data that has not yet been written to the database
    /// @param num : [in] block heigth in database
    /// @param binLogData : [out] missing data of each binlog block
    /// @return true : it's completely written
    /// @return false : missing binlog in database
    bool getMissingBlocksFromBinLog(
        int64_t num, std::map<int64_t, std::vector<TableData::Ptr>> binLogData);

private:
    /// write data interface
    /// @param buffer : [in/out] data buffer
    /// @param ui/str : [in] data to write
    void writeUINT32(bytes& buffer, uint32_t ui);
    void writeUINT64(bytes& buffer, uint64_t ui);
    void writeString(bytes& buffer, const std::string& str);
    /// readUINT32 and readUINT64
    /// @param buffer : [in] data buffer
    /// @param offset : [in/out] data offset in buffer, will be increased after read
    /// @return : data need to read
    uint32_t readUINT32(const bytes& buffer, uint32_t& offset);
    uint64_t readUINT64(const bytes& buffer, uint32_t& offset);
    /// readString
    /// @param buffer : [in] data buffer
    /// @param str : [out] data need to read
    /// @param offset : [in/out] data offset in buffer, will be increased after read
    void readString(const bytes& buffer, std::string& str, uint32_t& offset);
    void encodeEntries(
        const std::vector<std::string>& vecField, Entries::Ptr entries, bytes& buffer);
    void encodeTable(TableData::Ptr table, bytes& buffer);
    void encodeBlock(int64_t num, const std::vector<TableData::Ptr>& datas, bytes& buffer);

    uint32_t m_writtenBytesLength = 0;  // length already written
    std::fstream m_outBinaryFile;       // the file being written
    std::string m_path;                 // storage path of binlog

    const uint32_t BINLOG_FILE_MAX_SIZE = 256 * 1024 * 1024;  // the max size of binlog file
    const uint32_t BINLOG_VERSION = 1;                        // binlog version
};
}  // namespace storage
}  // namespace dev
