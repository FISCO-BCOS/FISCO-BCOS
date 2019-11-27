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
/** @file SQLStorage.cpp
 *  @author ancelmo
 *  @date 20190324
 */

#include "StorageException.h"

#include <libchannelserver/ChannelRPCServer.h>

#include "Common.h"
#include "SQLStorage.h"
#include "Table.h"
#include <libchannelserver/ChannelMessage.h>
#include <libdevcore/FixedHash.h>

using namespace dev;
using namespace std;
using namespace dev::storage;

SQLStorage::SQLStorage() {}

Entries::Ptr SQLStorage::select(
    int64_t num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    try
    {
        STORAGE_EXTERNAL_LOG(TRACE) << "Query AMOPDB data";
        Json::Value requestJson;
        if (g_BCOSConfig.version() <= RC3_VERSION)
        {
            requestJson["op"] = "select";
        }
        else
        {
            requestJson["op"] = "select2";
        }
        // TODO: remove params blockhash
        requestJson["params"]["blockHash"] = "0";
        requestJson["params"]["num"] = num;
        requestJson["params"]["table"] = tableInfo->name;
        requestJson["params"]["key"] = key;

        if (condition)
        {
            for (auto it : *(condition))
            {
                Json::Value cond;
                cond.append(it.first);
                if (it.second.left.second == it.second.right.second && it.second.left.first &&
                    it.second.right.first)
                {
                    cond.append(Condition::eq);
                    cond.append(it.second.left.second);
                }
                else
                {
                    if (it.second.left.second != condition->unlimitedField())
                    {
                        if (it.second.left.first)
                        {
                            cond.append(Condition::ge);
                        }
                        else
                        {
                            cond.append(Condition::gt);
                        }
                        cond.append(it.second.left.second);
                    }

                    if (it.second.right.second != condition->unlimitedField())
                    {
                        if (it.second.right.first)
                        {
                            cond.append(Condition::le);
                        }
                        else
                        {
                            cond.append(Condition::lt);
                        }
                        cond.append(it.second.right.second);
                    }
                }
                // cond.append(it.second.left);
                // cond.append(it.second.second);
                requestJson["params"]["condition"].append(cond);
            }
        }

        Json::Value responseJson = requestDB(requestJson);

        int code = responseJson["code"].asInt();
        if (code != 0)
        {
            STORAGE_EXTERNAL_LOG(ERROR) << "Remote database return error:" << code;

            BOOST_THROW_EXCEPTION(StorageException(
                -1, "Remote database return error:" + boost::lexical_cast<std::string>(code)));
        }

        Entries::Ptr entries = std::make_shared<Entries>();
        if (g_BCOSConfig.version() <= RC3_VERSION)
        {
            std::vector<std::string> columns;
            for (Json::ArrayIndex i = 0; i < responseJson["result"]["columns"].size(); ++i)
            {
                std::string fieldName = responseJson["result"]["columns"].get(i, "").asString();
                columns.push_back(fieldName);
            }

            for (Json::ArrayIndex i = 0; i < responseJson["result"]["data"].size(); ++i)
            {
                Json::Value line = responseJson["result"]["data"].get(i, "");
                Entry::Ptr entry = std::make_shared<Entry>();

                for (Json::ArrayIndex j = 0; j < line.size(); ++j)
                {
                    std::string fieldValue = line.get(j, "").asString();

                    if (columns[j] == ID_FIELD)
                    {
                        entry->setID(fieldValue);
                    }
                    else if (columns[j] == NUM_FIELD)
                    {
                        entry->setNum(fieldValue);
                    }
                    else if (columns[j] == STATUS)
                    {
                        entry->setStatus(fieldValue);
                    }
                    else
                    {
                        entry->setField(columns[j], fieldValue);
                    }
                }

                if (entry->getStatus() == 0)
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }
        else
        {
            for (Json::ArrayIndex i = 0; i < responseJson["result"]["columnValue"].size(); ++i)
            {
                Json::Value line = responseJson["result"]["columnValue"][i];
                Entry::Ptr entry = std::make_shared<Entry>();

                for (auto key : line.getMemberNames())
                {
                    entry->setField(key, line.get(key, "").asString());
                }
                entry->setID(line.get(ID_FIELD, "").asString());
                entry->setNum(line.get(NUM_FIELD, "").asString());
                entry->setStatus(line.get(STATUS, "").asString());

                if (entry->getStatus() == 0)
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }
        return entries;
    }
    catch (std::exception& e)
    {
        STORAGE_EXTERNAL_LOG(ERROR) << "Query database error:" << e.what();

        BOOST_THROW_EXCEPTION(
            StorageException(-1, std::string("Query database error:") + e.what()));
    }

    return Entries::Ptr();
}

size_t SQLStorage::commit(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    try
    {
        STORAGE_EXTERNAL_LOG(DEBUG) << "Commit data to database:" << datas.size();

        if (datas.size() == 0)
        {
            STORAGE_EXTERNAL_LOG(DEBUG) << "Empty data.";

            return 0;
        }

        Json::Value requestJson;

        requestJson["op"] = "commit";
        // TODO: check if this param used
        requestJson["params"]["blockHash"] = "0";
        requestJson["params"]["num"] = num;

        for (auto it : datas)
        {
            Json::Value tableData;
            auto tableInfo = it->info;
            tableData["table"] = tableInfo->name;

            for (size_t i = 0; i < it->dirtyEntries->size(); ++i)
            {
                auto entry = it->dirtyEntries->get(i);

                Json::Value value;

                for (auto fieldIt : *entry)
                {
                    value[fieldIt.first] = fieldIt.second;
                }

                value[ID_FIELD] = boost::lexical_cast<std::string>(entry->getID());
                value[STATUS] = boost::lexical_cast<std::string>(entry->getStatus());

                tableData["entries"].append(value);
            }

            for (size_t i = 0; i < it->newEntries->size(); ++i)
            {
                auto entry = it->newEntries->get(i);

                Json::Value value;

                for (auto fieldIt : *entry)
                {
                    value[fieldIt.first] = fieldIt.second;
                }

                value[ID_FIELD] = boost::lexical_cast<std::string>(entry->getID());
                value[STATUS] = boost::lexical_cast<std::string>(entry->getStatus());

                tableData["entries"].append(value);
            }

            if (!tableData["entries"].empty())
            {
                requestJson["params"]["data"].append(tableData);
            }
        }

        Json::Value responseJson = requestDB(requestJson);

        int code = responseJson["code"].asInt();
        if (code != 0)
        {
            STORAGE_EXTERNAL_LOG(ERROR) << "Remote database return error:" << code;

            BOOST_THROW_EXCEPTION(StorageException(
                -1, "Remote database return error:" + boost::lexical_cast<std::string>(code)));
        }

        int count = responseJson["result"]["count"].asInt();

        return count;
    }
    catch (std::exception& e)
    {
        STORAGE_EXTERNAL_LOG(ERROR) << "Commit data to database error:" << e.what();

        BOOST_THROW_EXCEPTION(
            StorageException(-1, std::string("Commit data to database error:") + e.what()));
    }

    return 0;
}

TableData::Ptr SQLStorage::selectTableDataByNum(int64_t num, TableInfo::Ptr tableInfo)
{
    try
    {
        STORAGE_EXTERNAL_LOG(INFO) << LOG_DESC("Query AMOPDB data call selectTableDataByNum")
                                   << LOG_KV("tableName", tableInfo->name) << LOG_KV("num", num);
        Json::Value requestJson;
        requestJson["op"] = "selectbynum";
        requestJson["params"]["tableName"] = tableInfo->name;
        requestJson["params"]["num"] = num;

        Json::Value responseJson = requestDB(requestJson);
        int code = responseJson["code"].asInt();
        std::string message = responseJson["message"].asString();
        if (code != 0)
        {
            STORAGE_EXTERNAL_LOG(ERROR) << LOG_KV("Remote database return error code", code)
                                        << LOG_KV("error message", message);
            BOOST_THROW_EXCEPTION(StorageException(-1, "Remote database return error:" + message));
        }

        TableData::Ptr tableData = std::make_shared<TableData>();
        tableInfo->fields.emplace_back(tableInfo->key);
        tableInfo->fields.emplace_back(STATUS);
        tableInfo->fields.emplace_back(NUM_FIELD);
        tableInfo->fields.emplace_back(ID_FIELD);
        tableInfo->fields.emplace_back("_hash_");
        tableData->info = tableInfo;
        STORAGE_EXTERNAL_LOG(TRACE)
            << LOG_DESC("fields in table") << LOG_KV("table", tableInfo->name)
            << LOG_KV("size", tableInfo->fields.size());
        for (Json::ArrayIndex i = 0; i < responseJson["result"].size(); ++i)
        {
            Json::Value line = responseJson["result"][i];
            Entry::Ptr entry = std::make_shared<Entry>();
            for (auto key : line.getMemberNames())
            {
                if (std::find(tableInfo->fields.begin(), tableInfo->fields.end(), key) !=
                    tableInfo->fields.end())
                {
                    entry->setField(key, line.get(key, "").asString());
                }
                else
                {
                    STORAGE_EXTERNAL_LOG(ERROR)
                        << LOG_DESC("Invalid key in table") << LOG_KV("table", tableInfo->name)
                        << LOG_KV("key", key);
                    // BOOST_THROW_EXCEPTION(runtime_error("Invalid key in table"));
                }
            }
            entry->setID(line.get(ID_FIELD, "").asString());
            entry->setNum(line.get(NUM_FIELD, "").asString());
            entry->setStatus(line.get(STATUS, "").asString());

            if (entry->getStatus() == 0)
            {
                entry->setDirty(false);
                tableData->newEntries->addEntry(entry);
            }
        }
        tableData->dirtyEntries = std::make_shared<Entries>();
        return tableData;
    }
    catch (std::exception& e)
    {
        STORAGE_EXTERNAL_LOG(ERROR) << "Query database error:" << e.what();

        BOOST_THROW_EXCEPTION(
            StorageException(-1, std::string("Query database error:") + e.what()));
    }

    return TableData::Ptr();
}

Json::Value SQLStorage::requestDB(const Json::Value& value)
{
    int retry = 1;

    while (true)
    {
        try
        {
            dev::channel::TopicChannelMessage::Ptr request =
                std::make_shared<dev::channel::TopicChannelMessage>();
            request->setType(channel::AMOP_REQUEST);
            request->setSeq(dev::newSeq());

            std::stringstream ssOut;
            ssOut << value;

            auto str = ssOut.str();
            STORAGE_EXTERNAL_LOG(TRACE) << "Request AMOPDB:" << request->seq() << " " << str;


            dev::channel::TopicChannelMessage::Ptr response;

            STORAGE_EXTERNAL_LOG(TRACE) << "Retry Request amdb :" << retry;
            request->setTopicData(m_topic, (const byte*)str.data(), str.size());
            response = m_channelRPCServer->pushChannelMessage(request, m_timeout);
            if (response.get() == NULL || response->result() != 0)
            {
                STORAGE_EXTERNAL_LOG(ERROR) << "requestDB error:" << response->result();

                BOOST_THROW_EXCEPTION(
                    StorageException(-1, "Remote database return error:" +
                                             boost::lexical_cast<std::string>(response->result())));
            }

            // resolving topic
            std::string topic = response->topic();
            STORAGE_EXTERNAL_LOG(TRACE) << "Receive topic:" << topic;

            std::stringstream ssIn;
            std::string jsonStr(response->data(), response->data() + response->dataSize());
            ssIn << jsonStr;

            STORAGE_EXTERNAL_LOG(TRACE) << "amdb-proxy Response:" << ssIn.str();

            Json::Value responseJson;
            ssIn >> responseJson;

            auto codeValue = responseJson["code"];
            if (!codeValue.isInt())
            {
                BOOST_THROW_EXCEPTION(StorageException(-1, "undefined amdb error code"));
            }

            int code = codeValue.asInt();
            if (code == 1)
            {
                BOOST_THROW_EXCEPTION(StorageException(
                    1, "amdb sql error:" + boost::lexical_cast<std::string>(code)));
            }
            if (code != 0 && code != 1)
            {
                BOOST_THROW_EXCEPTION(StorageException(
                    -1, "amdb code error:" + boost::lexical_cast<std::string>(code)));
            }

            return responseJson;
        }
        catch (dev::channel::ChannelException& e)
        {
            STORAGE_EXTERNAL_LOG(ERROR)
                << "ChannelException error: " << boost::diagnostic_information(e);
            STORAGE_EXTERNAL_LOG(ERROR) << "Retrying..." << LOG_KV("count", retry);
        }
        catch (StorageException& e)
        {
            if (e.errorCode() == -1)
            {
                STORAGE_EXTERNAL_LOG(ERROR) << "AMDB-proxy error: " << e.what();
                STORAGE_EXTERNAL_LOG(ERROR) << "Retrying..." << LOG_KV("count", retry);
            }
            else
            {
                STORAGE_EXTERNAL_LOG(ERROR) << "unknow AMDB-proxy error: " << e.what();
                BOOST_THROW_EXCEPTION(e);
            }
        }

        ++retry;
        if (m_maxRetry != 0 && retry >= m_maxRetry)
        {
            STORAGE_EXTERNAL_LOG(ERROR) << "SQLStorage unreachable" << LOG_KV("maxRetry", retry);
            // The SQLStorage unreachable, the program will exit with abnormal status
            auto e = StorageException(-1, "Reach max retry");
            std::cout << "The sqlstorage doesn't work well,"
                      << "the fisco-bcos will exit." << std::endl;

            m_fatalHandler(e);

            BOOST_THROW_EXCEPTION(e);
        }

        this_thread::sleep_for(chrono::milliseconds(1000));
    }
}

void SQLStorage::setTopic(const std::string& topic)
{
    m_topic = topic;
}

void SQLStorage::setChannelRPCServer(dev::ChannelRPCServer::Ptr channelRPCServer)
{
    m_channelRPCServer = channelRPCServer;
}

void SQLStorage::setMaxRetry(int maxRetry)
{
    m_maxRetry = maxRetry;
}
