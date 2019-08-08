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

#include "BinLogHandler.h"
#include <json/json.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <fstream>

namespace fs = boost::filesystem;

using namespace dev;
using namespace dev::storage;

BinLogHandler::BinLogHandler(const std::string& path)
{
    setBinLogStoragePath(path);
}

BinLogHandler::~BinLogHandler()
{
    if (m_outBinaryFile.is_open())
    {
        m_outBinaryFile.close();
    }
}

void BinLogHandler::setBinLogStoragePath(const std::string& path)
{
    // called during chain initialization
    m_path = path;
    // If the directory exists, the function does nothing
    fs::create_directory(m_path);
}

bool BinLogHandler::writeBlocktoBinLog(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    bytes buffer;
    encodeBlock(num, datas, buffer);

    if (m_writtenBytesLength == 0 || m_writtenBytesLength + buffer.size() > BINLOG_FILE_MAX_SIZE)
    {  // check if need to create a new file, in the case of first write or capacity limitation
        if (m_writtenBytesLength > 0 && m_outBinaryFile.is_open())
        {  // close the file which has been opened
            m_outBinaryFile.close();
        }

        std::string filePath = m_path + std::to_string(num) + ".binlog";
        m_outBinaryFile.open(filePath, std::ios::out | std::ios::binary);
        if (m_outBinaryFile && m_outBinaryFile.is_open())
        {
            BINLOG_HANDLER_LOG(INFO)
                << LOG_DESC("open binary file successful!") << LOG_KV("fileName", buffer.size())
                << LOG_KV("writtenBytesLength", m_writtenBytesLength);
            m_writtenBytesLength = 0;
            // TODO:write version
        }
        else
        {
            BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("open binary file fail!");
            return false;
        }
    }
    // write block buffer, include block length, block buffer and CRC32
    m_outBinaryFile.write((char*)&buffer[0], buffer.size());
    m_outBinaryFile.flush();
    m_writtenBytesLength += buffer.size();
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("binlog binary current size")
                             << LOG_KV("size", m_writtenBytesLength);

    return true;
}

bool BinLogHandler::getMissingBlocksFromBinLog(
    int64_t, std::map<int64_t, std::vector<TableData::Ptr>>)
{
    return true;
}

void BinLogHandler::encodeBlock(int64_t, const std::vector<TableData::Ptr>&, bytes&) {}
