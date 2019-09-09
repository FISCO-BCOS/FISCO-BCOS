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
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem.hpp>

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
    m_path = path + "/";
    // If the directory exists, the function does nothing
    fs::create_directories(m_path);
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("set binLog storage path") << LOG_KV("path", m_path);
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

        BINLOG_HANDLER_LOG(INFO) << LOG_DESC("try to open new binary file!")
                                 << LOG_KV("file written length", m_writtenBytesLength)
                                 << LOG_KV("buffer size", buffer.size());
        // open binary file and write version
        if (!initNewBinaryFile(num))
        {
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

std::shared_ptr<BlockDataMap> BinLogHandler::getMissingBlocksFromBinLog(int64_t _currentNum)
{
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("get missing blocks")
                             << LOG_KV("current database num", _currentNum);
    std::shared_ptr<BlockDataMap> binLogData = std::make_shared<BlockDataMap>();
    fs::path path(m_path);
    if (fs::is_directory(path))
    {
        try
        {
            bool incompleteData = true;
            typedef std::vector<fs::path> vec;
            vec v;
            // descending sort, read from first
            std::copy(fs::directory_iterator(path), fs::directory_iterator(), back_inserter(v));
            if (v.size() > 0)
            {
                std::sort(v.begin(), v.end(), [](fs::path const& a, fs::path const& b) {
                    return std::stoll(a.filename().string()) >= std::stoll(b.filename().string());
                });
                for (vec::const_iterator it(v.begin()), it_end(v.end()); it != it_end; ++it)
                {
                    BINLOG_HANDLER_LOG(INFO)
                        << LOG_DESC("binlog log path") << LOG_KV("path", it->string());
                    if (getFirstBlockNumInBinLog(it->string()) !=
                        std::stoll(it->filename().string()))
                    {
                        BOOST_THROW_EXCEPTION(dev::StorageError() << errinfo_comment(
                                                  "the first block num in binlog is not equal to "
                                                  "file name! file name:" +
                                                  it->filename().string()));
                    }
                    readBinLog(it->string(), _currentNum, *binLogData);
                    if (_currentNum >= std::stoll(it->filename().string()))
                    {
                        incompleteData = false;
                        break;
                    }
                }
            }
            else
            {
                incompleteData = false;
            }
            // the minimum block height written is 0, but the input may be less than 0
            if (incompleteData && _currentNum >= 0)
            {
                BOOST_THROW_EXCEPTION(
                    dev::StorageError() << errinfo_comment(
                        "the database block height is less than the binlog record's minimum block "
                        "height"));
            }
        }
        catch (std::exception& e)
        {
            BOOST_THROW_EXCEPTION(
                dev::StorageError() << errinfo_comment(boost::diagnostic_information(e)));
        }
    }

    return binLogData;
}

bool BinLogHandler::initNewBinaryFile(int64_t num)
{
    std::string filePath = m_path + std::to_string(num) + ".binlog";
    m_outBinaryFile.open(filePath, std::ios::out | std::ios::binary);
    if (!m_outBinaryFile || !m_outBinaryFile.is_open())
    {
        BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("open binary file fail!");
        return false;
    }
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("open binary file success!")
                             << LOG_KV("file path", filePath);

    // write version
    uint32_t version = htonl(BINLOG_VERSION);
    m_outBinaryFile.write((char*)&version, sizeof(uint32_t));
    m_writtenBytesLength = sizeof(uint32_t);

    return true;
}

void BinLogHandler::writeUINT32(bytes& buffer, uint32_t ui)
{
    uint32_t ui_tmp = htonl(ui);
    buffer.insert(buffer.end(), (byte*)&ui_tmp, (byte*)&ui_tmp + 4);
}

void BinLogHandler::writeUINT64(bytes& buffer, uint64_t ui)
{
    uint64_t ui_tmp = HTONLL(ui);
    buffer.insert(buffer.end(), (byte*)&ui_tmp, (byte*)&ui_tmp + 8);
}

void BinLogHandler::writeString(bytes& buffer, const std::string& str)
{
    writeUINT32(buffer, str.length());
    buffer.insert(buffer.end(), str.begin(), str.end());
}

uint32_t BinLogHandler::readUINT32(const bytes& buffer, uint32_t& offset)
{
    uint32_t ui = ntohl(*((uint32_t*)&buffer[offset]));
    offset += 4;
    return ui;
}

uint64_t BinLogHandler::readUINT64(const bytes& buffer, uint32_t& offset)
{
    uint64_t ui = NTOHLL(*((uint64_t*)&buffer[offset]));
    offset += 8;
    return ui;
}

void BinLogHandler::readString(const bytes& buffer, std::string& str, uint32_t& offset)
{
    uint32_t strLen = readUINT32(buffer, offset);
    std::string s((char*)&buffer[offset], strLen);
    str = s;
    offset += strLen;
}

void BinLogHandler::encodeEntries(
    const std::vector<std::string>& vecField, Entries::Ptr entries, bytes& buffer)
{
    writeUINT32(buffer, entries->size());
    for (size_t idx = 0; idx < entries->size(); idx++)
    {
        auto entry = entries->get(idx);
        writeUINT64(buffer, entry->getID());
        uint8_t status = entry->getStatus();
        BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("entry info") << LOG_KV("idx", idx)
                                  << LOG_KV("id", entry->getID())
                                  << LOG_KV("status", (uint32_t)status);
        buffer.insert(buffer.end(), (byte*)&status, (byte*)&status + 1);
        for (const auto& key : vecField)
        {
            if (key == STATUS || key == NUM_FIELD || key == ID_FIELD)
            {
                continue;
            }
            std::string value = "";
            auto fieldIt = entry->find(key);
            if (fieldIt != entry->end())
            {
                value = fieldIt->second;
            }
            BINLOG_HANDLER_LOG(TRACE) << "key:" << key << ", value:" << value;
            writeString(buffer, value);
        }
    }
}

void BinLogHandler::encodeTable(TableData::Ptr table, bytes& buffer)
{
    // table info: name and fields
    TableInfo::Ptr info = table->info;
    uint8_t tableNameLen = info->name.length();
    buffer.insert(buffer.end(), (byte*)&tableNameLen, (byte*)&tableNameLen + 1);
    buffer.insert(buffer.end(), info->name.begin(), info->name.end());
    std::vector<std::string> vecField;  // record a list of column names used in encodeEntries
    std::stringstream ss;               // used to output column names
    ss << info->key;
    vecField.push_back(info->key);
    for (size_t j = 0; j < info->fields.size(); j++)
    {
        if (info->fields[j] != info->key)
        {
            ss << "," << info->fields[j];
            vecField.push_back(info->fields[j]);
        }
    }
    writeString(buffer, ss.str());
    BINLOG_HANDLER_LOG(TRACE) << "table name:" << info->name << ",fields(include key):" << ss.str();
    // table data : dirty entries and new entries
    encodeEntries(vecField, table->dirtyEntries, buffer);
    encodeEntries(vecField, table->newEntries, buffer);
}

void BinLogHandler::encodeBlock(
    int64_t num, const std::vector<TableData::Ptr>& datas, bytes& buffer)
{
    // block heigth
    writeUINT64(buffer, num);

    // block data
    writeUINT32(buffer, datas.size());
    for (size_t i = 0; i < datas.size(); i++)
    {
        encodeTable(datas[i], buffer);
    }

    // CRC32
    boost::crc_32_type result;
    result.process_block((char*)&buffer[0], (char*)&buffer[0] + buffer.size());
    uint32_t crc32 = result.checksum();
    BINLOG_HANDLER_LOG(DEBUG) << LOG_KV("CRC32", crc32);
    writeUINT32(buffer, crc32);

    // insert length
    uint32_t length = htonl(buffer.size());
    buffer.insert(buffer.begin(), (byte*)&length, (byte*)&length + 4);
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("block binary data length include header and CRC32")
                             << LOG_KV("num", num) << LOG_KV("size", buffer.size())
                             << LOG_KV("CRC32", crc32);
}

uint32_t BinLogHandler::decodeEntries(const bytes& buffer, uint32_t& offset,
    const std::vector<std::string>& vecField, Entries::Ptr entries)
{
    uint32_t preOffset = offset;
    uint32_t entryCount = readUINT32(buffer, offset);
    for (uint32_t idx = 0; idx < entryCount; idx++)
    {
        Entry::Ptr entry = std::make_shared<Entry>();
        uint64_t id = readUINT64(buffer, offset);
        entry->setID(id);
        uint8_t status = *((uint8_t*)&buffer[offset]);
        offset++;
        entry->setStatus(status);
        BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("entry info") << LOG_KV("idx in entries", idx)
                                  << LOG_KV("_id_", entry->getID())
                                  << LOG_KV("_status_", (uint32_t)entry->getStatus());
        for (const auto& key : vecField)
        {
            if (key == STATUS || key == NUM_FIELD || key == ID_FIELD)
            {
                continue;
            }
            std::string value;
            readString(buffer, value, offset);
            if (value != "")
            {
                entry->setField(key, value);
            }
            BINLOG_HANDLER_LOG(TRACE) << "key:" << key << ", value:" << value;
        }
        entries->addEntry(entry);
    }
    return offset - preOffset;
}

uint32_t BinLogHandler::decodeTable(const bytes& buffer, uint32_t& offset, TableData::Ptr data)
{
    uint32_t preOffset = offset;
    // table name
    uint8_t tableNameLen = *((uint8_t*)&buffer[offset]);
    offset++;
    std::string s((char*)&buffer[offset], tableNameLen);
    data->info->name = s;
    offset += tableNameLen;
    // table key and fields
    std::string fields;
    readString(buffer, fields, offset);
    std::vector<std::string> vecField;
    boost::split(vecField, fields, boost::is_any_of(","), boost::token_compress_on);
    data->info->key = vecField[0];
    data->info->fields.insert(
        data->info->fields.end(), vecField.begin() + 1, vecField.begin() + vecField.size());
    return offset - preOffset;
}

DecodeBlockResult BinLogHandler::decodeBlock(
    int64_t currentNum, const bytes& buffer, int64_t& binLogNum, std::vector<TableData::Ptr>& datas)
{
    uint32_t offset = 0;
    binLogNum = readUINT64(buffer, offset);
    if (binLogNum <= currentNum)
    {
        BINLOG_HANDLER_LOG(DEBUG) << LOG_DESC("decode block, the block height has been written!")
                                  << LOG_KV("block height", binLogNum);
        return DecodeBlockResult::WrittenBlock;
    }
    uint32_t dataSize = readUINT32(buffer, offset);
    for (uint32_t i = 0; i < dataSize; i++)
    {
        TableData::Ptr data = std::make_shared<TableData>();
        uint32_t tableSize = decodeTable(buffer, offset, data);

        // entries
        std::vector<std::string> vecField;
        vecField.push_back(data->info->key);
        for (const auto& field : data->info->fields)
        {
            vecField.push_back(field);
        }
        uint32_t dirtySize = decodeEntries(buffer, offset, vecField, data->dirtyEntries);
        uint32_t newSize = decodeEntries(buffer, offset, vecField, data->newEntries);
        BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("decode block") << LOG_KV("idx", i)
                                  << LOG_KV("tableInfo buffer size", tableSize)
                                  << LOG_KV("dirtyEntries buffer size", dirtySize)
                                  << LOG_KV("newEntries buffer size", newSize);

        datas.push_back(data);
    }
    BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("decode block end")
                              << LOG_KV("buffer size", buffer.size())
                              << LOG_KV("buffer used index", offset);

    if (buffer.size() == offset + 4)
    {  // The remaining 4 bytes is CRC32
        return DecodeBlockResult::Success;
    }
    else
    {
        BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("decode block, block data size error!");
        return DecodeBlockResult::BlockDataLengthError;
    }
}

bool BinLogHandler::getBinLogContext(BinLogContext& binlog)
{
    // get length
    binlog.file.seekg(0, std::ios_base::end);
    binlog.length = (uint32_t)binlog.file.tellg();
    if (binlog.length < sizeof(BINLOG_VERSION))
    {
        BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("read binLog error!")
                                  << LOG_KV("length", binlog.length);
        return false;
    }
    binlog.file.seekg(0);
    // get version
    uint32_t size =
        binlog.file.read(reinterpret_cast<char*>(&binlog.version), sizeof(uint32_t)).gcount();
    if (size != sizeof(uint32_t))
    {
        BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("read binLog error!");
        return false;
    }
    binlog.version = ntohl(binlog.version);
    binlog.offset += sizeof(uint32_t);
    return true;
}

bool BinLogHandler::getBlockData(
    BinLogContext& binlog, int64_t currentNum, BlockDataMap& blocksData)
{
    while (binlog.offset < binlog.length)
    {
        // get block data length
        uint32_t blockLen = 0;
        uint32_t size =
            binlog.file.read(reinterpret_cast<char*>(&blockLen), sizeof(uint32_t)).gcount();
        if (size != sizeof(uint32_t))
        {
            BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("read binLog error!");
            return false;
        }
        blockLen = ntohl(blockLen);
        BINLOG_HANDLER_LOG(TRACE) << LOG_DESC("decode block")
                                  << LOG_KV("block index", binlog.offset)
                                  << LOG_KV("block data length", blockLen);
        binlog.offset += sizeof(uint32_t);
        if (binlog.offset + blockLen > binlog.length)
        {
            BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("readBinLog, block data length error!");
            return false;
        }

        // get block buffer, include CRC32
        bytes buffer;
        buffer.resize(blockLen);
        size = binlog.file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()).gcount();
        if (size != buffer.size())
        {
            BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("readBinLog, read error!");
            return false;
        }
        binlog.offset += blockLen;
        int64_t num;
        std::vector<TableData::Ptr> datas;
        DecodeBlockResult decodeRet = decodeBlock(currentNum, buffer, num, datas);
        if (DecodeBlockResult::Success == decodeRet)
        {
            blocksData[num] = datas;
        }
        else if (DecodeBlockResult::BlockDataLengthError == decodeRet)
        {
            return false;
        }
    }
    return true;
}

bool BinLogHandler::readBinLog(
    const std::string& filePath, int64_t currentNum, BlockDataMap& blocksData)
{
    BinLogContext binlog(filePath);
    if (!binlog.file.is_open() || !getBinLogContext(binlog))
    {
        return false;
    }
    BINLOG_HANDLER_LOG(INFO) << LOG_DESC("readBinLog start") << LOG_KV("file length", binlog.length)
                             << LOG_KV("version", binlog.version)
                             << LOG_KV("offset", binlog.offset);
    bool ret = false;
    if (binlog.version == BINLOG_VERSION)
    {
        ret = getBlockData(binlog, currentNum, blocksData);
    }
    return ret;
}

int64_t BinLogHandler::getFirstBlockNumInBinLog(const std::string& filePath)
{
    BinLogContext binlog(filePath);
    if (!binlog.file.is_open() || !getBinLogContext(binlog))
    {
        return -1;
    }
    uint32_t blockLen = 0;
    uint64_t blockNum = 0;
    if (binlog.file.read(reinterpret_cast<char*>(&blockLen), sizeof(uint32_t)).gcount() !=
            sizeof(uint32_t) ||
        binlog.file.read(reinterpret_cast<char*>(&blockNum), sizeof(uint64_t)).gcount() !=
            sizeof(uint64_t))
    {
        BINLOG_HANDLER_LOG(ERROR) << LOG_DESC("read binLog error!");
        return -1;
    }
    blockNum = NTOHLL(blockNum);
    return blockNum;
}
