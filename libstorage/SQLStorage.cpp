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
#include <libdevcore/easylog.h>

#include "Common.h"
#include "SQLStorage.h"
#include "Table.h"
#include <libchannelserver/ChannelMessage.h>
#include <libdevcore/FixedHash.h>

using namespace dev;
using namespace dev::storage;

SQLStorage::SQLStorage() {}

Entries::Ptr SQLStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    try
    {
        LOG(TRACE) << "Query AMOPDB data";
        Json::Value requestJson;

        requestJson["op"] = "select";
        requestJson["params"]["blockHash"] = hash.hex();
        requestJson["params"]["num"] = num;
        requestJson["params"]["table"] = tableInfo->name;
        requestJson["params"]["key"] = key;

        if (condition)
        {
            for (auto it : *(condition->getConditions()))
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
            LOG(ERROR) << "Remote database return error:" << code;

            throw StorageException(
                -1, "Remote database return error:" + boost::lexical_cast<std::string>(code));
        }

        std::vector<std::string> columns;
        for (Json::ArrayIndex i = 0; i < responseJson["result"]["columns"].size(); ++i)
        {
            std::string fieldName = responseJson["result"]["columns"].get(i, "").asString();
            columns.push_back(fieldName);
        }

        Entries::Ptr entries = std::make_shared<Entries>();
        for (Json::ArrayIndex i = 0; i < responseJson["result"]["data"].size(); ++i)
        {
            Json::Value line = responseJson["result"]["data"].get(i, "");
            Entry::Ptr entry = std::make_shared<Entry>();

            for (Json::ArrayIndex j = 0; j < line.size(); ++j)
            {
                std::string fieldValue = line.get(j, "").asString();

                entry->setField(columns[j], fieldValue);
            }

            if (entry->getStatus() == 0)
            {
                entry->setDirty(false);
                entries->addEntry(entry);
            }
        }

        entries->setDirty(false);
        return entries;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Query database error:" << e.what();

        throw StorageException(-1, std::string("Query database error:") + e.what());
    }

    return Entries::Ptr();
}

size_t SQLStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    try
    {
        LOG(DEBUG) << "Commit data to database:" << datas.size();

        if (datas.size() == 0)
        {
            LOG(DEBUG) << "Empty data.";

            return 0;
        }

        Json::Value requestJson;

        requestJson["op"] = "commit";
        requestJson["params"]["blockHash"] = hash.hex();
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

                for (auto fieldIt : *entry->fields())
                {
                    value[fieldIt.first] = fieldIt.second;
                }

                tableData["entries"].append(value);
            }

            for (size_t i = 0; i < it->newEntries->size(); ++i)
            {
                auto entry = it->newEntries->get(i);

                Json::Value value;

                for (auto fieldIt : *entry->fields())
                {
                    value[fieldIt.first] = fieldIt.second;
                }

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
            LOG(ERROR) << "Remote database return error:" << code;

            throw StorageException(
                -1, "Remote database return error:" + boost::lexical_cast<std::string>(code));
        }

        int count = responseJson["result"]["count"].asInt();

        return count;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Commit data to database error:" << e.what();

        throw StorageException(-1, std::string("Commit data to database error:") + e.what());
    }

    return 0;
}

bool SQLStorage::onlyDirty()
{
    return true;
}

Json::Value SQLStorage::requestDB(const Json::Value& value)
{
    int retry = 0;

    while (true)
    {
        try
        {
            dev::channel::TopicChannelMessage::Ptr request =
                std::make_shared<dev::channel::TopicChannelMessage>();
            request->setType(0x30);
            request->setSeq(m_channelRPCServer->newSeq());

            std::stringstream ssOut;
            ssOut << value;

            auto str = ssOut.str();
            LOG(TRACE) << "Request AMOPDB:" << request->seq() << " " << str;

            request->setTopic(m_topic);

            dev::channel::TopicChannelMessage::Ptr response;

            LOG(TRACE) << "Retry Request amdb :" << retry;
            request->setData((const byte*)str.data(), str.size());
            response = m_channelRPCServer->pushChannelMessage(request);
            if (response.get() == NULL || response->result() != 0)
            {
                LOG(ERROR) << "requestDB error:" << response->result();

                throw StorageException(
                    -1, "Remote database return error:" +
                            boost::lexical_cast<std::string>(response->result()));
            }

            // resolving topic
            std::string topic = response->topic();
            LOG(TRACE) << "Receive topic:" << topic;

            std::stringstream ssIn;
            std::string jsonStr(response->data(), response->data() + response->dataSize());
            ssIn << jsonStr;

            LOG(TRACE) << "AMOPDB Response:" << ssIn.str();

            Json::Value responseJson;
            ssIn >> responseJson;

            auto codeValue = responseJson["code"];
            if (!codeValue.isInt())
            {
                throw StorageException(-1, "undefined amdb error code");
            }

            int code = codeValue.asInt();
            if (code == 1)
            {
                throw StorageException(
                    1, "amdb sql error:" + boost::lexical_cast<std::string>(code));
            }
            if (code != 0 && code != 1)
            {
                throw StorageException(
                    -1, "amdb code error:" + boost::lexical_cast<std::string>(code));
            }

            return responseJson;
        }
        catch (dev::channel::ChannelException& e)
        {
            LOG(ERROR) << "AMDB error: " << e.what();
            LOG(ERROR) << "Retrying...";
        }
        catch (StorageException& e)
        {
            if (e.errorCode() == -1)
            {
                LOG(ERROR) << "AMDB error: " << e.what();
                LOG(ERROR) << "Retrying...";
            }
            else
            {
                BOOST_THROW_EXCEPTION(e);
            }
        }

        ++retry;
        if (m_maxRetry != 0 && retry >= m_maxRetry)
        {
            LOG(ERROR) << "SQLStorage unreachable" << LOG_KV("maxRetry", retry);
            // The SQLStorage unreachable, the program will exit with abnormal status
            auto e = StorageException(-1, "Reach max retry");
            std::cout << "The sqlstorage doesn't work well,"
                      << "the program will exit with abnormal status" << std::endl;

            m_fatalHandler(e);

            BOOST_THROW_EXCEPTION(e);
        }

        sleep(1);
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
