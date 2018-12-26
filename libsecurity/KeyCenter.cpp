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
/**
 * @brief : keycenter for disk encrytion
 * @author: jimmyshi
 * @date: 2018-12-03
 */

#include "KeyCenter.h"
#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/SHA3.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/AES.h>

using namespace std;
using namespace dev;
using namespace jsonrpc;

const bytes KeyCenter::getDataKey(const std::string& _cipherDataKey)
{
    if (_cipherDataKey.empty())
    {
        LOG(DEBUG) << "[KeyCenter] Get datakey exception. cipherDataKey is empty" << endl;
        BOOST_THROW_EXCEPTION(KeyCenterDataKeyError());
    }

    if (m_lastQueryCipherDataKey == _cipherDataKey)
    {
        return m_lastRcvDataKey;
    }

    HttpClient client(m_url);
    Client node(client);

    string dataKeyBytesStr;
    try
    {
        Json::Value params(Json::arrayValue);
        params.append(_cipherDataKey);
        Json::Value rsp = node.CallMethod("decDataKey", params);

        int error = rsp["error"].asInt();
        dataKeyBytesStr = rsp["dataKey"].asString();
        string info = rsp["info"].asString();
        if (error)
        {
            LOG(DEBUG) << "[KeyCenter] Get datakey exception. keycentr info: " << info << endl;
            BOOST_THROW_EXCEPTION(KeyCenterConnectionError() << errinfo_comment(info));
        }
        // update query cache
        m_lastQueryCipherDataKey = _cipherDataKey;
        bytes readableDataKey = fromHex(dataKeyBytesStr);
        m_lastRcvDataKey = uniformDataKey(readableDataKey);
    }
    catch (exception& e)
    {
        LOG(DEBUG) << "[KeyCenter] Get datakey exception for: " << e.what() << endl;
        BOOST_THROW_EXCEPTION(KeyCenterConnectionError() << errinfo_comment(e.what()));
    }

    return m_lastRcvDataKey;
};

const std::string KeyCenter::generateCipherDataKey()
{
    std::string ret;
    for (size_t i = 0; i < 32; i++)
    {
        ret += std::to_string(utcTime() % 10);
    }
    return ret;
}

void KeyCenter::setIpPort(const std::string& _ip, int _port)
{
    m_url = _url;
    m_port = _port;
    LOG(DEBUG) << "[KeyCenter] Instance url: " << m_url << ":" << m_port << endl;
}

KeyCenter& KeyCenter::instance()
{
    static KeyCenter ins;
    return ins;
}

dev::bytes KeyCenter::uniformDataKey(const dev::bytes& _readableDataKey)
{
    // Uniform datakey to a fix size 32 bytes by hashing it
    // Because we has no limit to _readableDataKey size
    return sha3(ref(_readableDataKey)).asBytes();
}