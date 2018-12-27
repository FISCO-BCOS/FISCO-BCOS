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
#include "Common.h"
#include <json/json.h>
#include <jsonrpccpp/common/exception.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/AES.h>
#include <libdevcrypto/Hash.h>
#include <iostream>
#include <string>

using namespace std;
using namespace dev;
using namespace jsonrpc;

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
using tcp = net::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

KeyCenterHttpClient::KeyCenterHttpClient(const string& _ip, int _port)
  : m_ip(_ip), m_port(_port), m_ioc(), m_socket(m_ioc)
{}

KeyCenterHttpClient::~KeyCenterHttpClient()
{
    close();
}

void KeyCenterHttpClient::connect()
{
    WriteGuard(x_clinetSocket);
    try
    {
        if (m_socket.is_open())
            return;
        // These objects perform our I/O
        tcp::resolver resolver{m_ioc};
        // Look up the domain name
        auto const results = resolver.resolve(m_ip, to_string(m_port).c_str());

        // Make the connection on the IP address we get from a lookup
        // TODO Add timeout in connect and read write
        net::connect(m_socket, results.begin(), results.end());
    }
    catch (exception& e)
    {
        LOG(ERROR) << "[KeyCenter] Init keycenter failed for " << e.what() << endl;
        BOOST_THROW_EXCEPTION(KeyCenterInitError());
    }
}

void KeyCenterHttpClient::close()
{
    WriteGuard(x_clinetSocket);
    if (!m_socket.is_open())
        return;
    // Gracefully close the socket
    beast::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_both, ec);

    if (ec && ec != beast::errc::not_connected)
    {
        LOG(ERROR) << "[KeyCenter] Close keycenter failed. error_code " << ec << endl;
        BOOST_THROW_EXCEPTION(KeyCenterCloseError());
    }
}

Json::Value KeyCenterHttpClient::callMethod(const string& _method, Json::Value _params)
{
    if (!m_socket.is_open())
        connect();  // Jump out immediately if has connected

    Json::Value res;
    try
    {
        /*
        query is:
            {"jsonrpc":"2.0","method":"encDataKey","params":["123456"],"id":83}
        */

        Json::Value queryJson;
        queryJson["id"] = 83;
        queryJson["jsonrpc"] = "2.0";
        queryJson["method"] = _method;
        queryJson["params"] = _params;

        Json::FastWriter fastWriter;
        std::string queryJsonStr = fastWriter.write(queryJson);
        std::string url = m_ip + ":" + to_string(m_port);
        // std::cout << queryJsonStr << " length: " << queryJsonStr.length() << std::endl;

        http::request<http::string_body> req{http::verb::post, "/", 11};
        req.set(http::field::host, url.c_str());
        req.set(http::field::accept, "*/*");
        req.set(http::field::content_type, "application/json");
        req.set(http::field::accept_charset, "utf-8");

        req.body() = queryJsonStr.c_str();
        req.prepare_payload();

        // Send the HTTP request to the remote host
        http::write(m_socket, req);

        // This buffer is used for reading and must be persisted
        beast::flat_buffer buffer;

        // Declare a container to hold the response
        http::response<http::string_body> rsp;

        // Receive the HTTP response
        int rspSize = http::read(m_socket, buffer, rsp);

        LOG(DEBUG) << "[KeyCenter] [callMethod] keycenter respond [code/string]: "
                   << rsp.result_int() << "/" << rsp.body() << endl;

        if (rsp.result_int() != 200)
        {
            LOG(ERROR) << "[KeyCenter] [callMethod] http error: " << rsp.result_int() << endl;
            throw;
        }

        Json::Reader reader;
        bool parsingSuccessful = reader.parse(rsp.body().c_str(), res);
        if (!parsingSuccessful)
        {
            LOG(ERROR) << "[KeyCenter] [callMethod] respond json error: " << rsp.body() << endl;
            throw;
        }

        return res["result"];
    }
    catch (exception& e)
    {
        LOG(ERROR) << "[KeyCenter] CallMethod error: " << e.what() << endl;
        BOOST_THROW_EXCEPTION(KeyCenterConnectionError());
    }

    return res;
}

const bytes KeyCenter::getDataKey(const std::string& _cipherDataKey)
{
    if (_cipherDataKey.empty())
    {
        LOG(ERROR) << "[KeyCenter] Get datakey exception. cipherDataKey is empty" << endl;
        BOOST_THROW_EXCEPTION(KeyCenterDataKeyError());
    }

    if (m_lastQueryCipherDataKey == _cipherDataKey)
    {
        return m_lastRcvDataKey;
    }


    string dataKeyBytesStr;
    try
    {
        // connect
        KeyCenterHttpClient kcclient(m_ip, m_port);
        kcclient.connect();

        // send and receive
        Json::Value params(Json::arrayValue);
        params.append(_cipherDataKey);
        Json::Value rsp = kcclient.callMethod("decDataKey", params);

        // parse respond
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

        // close
        kcclient.close();
    }
    catch (exception& e)
    {
        clearCache();
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
    m_ip = _ip;
    m_port = _port;
    m_url = m_ip + ":" + std::to_string(m_port);
    LOG(DEBUG) << "[KeyCenter] Instance url: " << m_ip << ":" << m_port << endl;
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