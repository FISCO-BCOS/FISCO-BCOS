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
 * @file SafeHttpServer.cpp
 * @author: caryliao
 * @date 2018-10-26
 */

#include "SafeHttpServer.h"
#include "libdevcore/Log.h"
#include <arpa/inet.h>
#include <jsonrpccpp/common/specificationparser.h>
#include <netinet/in.h>
#include <sstream>

using namespace std;
using namespace dev;

/// structure copied from libjson-rpc-cpp httpserver version 0.6.0
struct mhd_coninfo
{
    struct MHD_PostProcessor* postprocessor;
    MHD_Connection* connection;
    stringstream request;
    SafeHttpServer* server;
    int code;
};

SafeHttpServer::SafeHttpServer(std::string const& _address, int _port, bool _ipv6,
    std::string const& _sslcert, std::string const& _sslkey, int _threads)
  : AbstractServerConnector(),
    port(_port),
    threads(_threads),
    running(false),
    ipv6Enabled(_ipv6),
    path_sslcert(_sslcert),
    path_sslkey(_sslkey),
    daemon(NULL),
    m_address(_address)
{}

jsonrpc::IClientConnectionHandler* SafeHttpServer::GetHandler(const std::string& url)
{
    if (AbstractServerConnector::GetHandler() != NULL)
        return AbstractServerConnector::GetHandler();
    map<string, jsonrpc::IClientConnectionHandler*>::iterator it = this->urlhandler.find(url);
    if (it != this->urlhandler.end())
        return it->second;
    return NULL;
}


bool SafeHttpServer::StartListening()
{
    if (!this->running)
    {
        if (this->path_sslcert != "" && this->path_sslkey != "")
        {
            try
            {
                jsonrpc::SpecificationParser::GetFileContent(this->path_sslcert, this->sslcert);
                jsonrpc::SpecificationParser::GetFileContent(this->path_sslkey, this->sslkey);
            }
            catch (jsonrpc::JsonRpcException& ex)
            {
                return false;
            }
        }


        int ioMultiDevice = MHD_USE_EPOLL_INTERNAL_THREAD;

#if __APPLE__
        ioMultiDevice = MHD_USE_POLL_INTERNAL_THREAD;

        LOG(INFO) << LOG_BADGE("SafeHttpServer::StartListening")
                  << LOG_DESC("use poll for apple macos platform");
#else
        LOG(INFO) << LOG_BADGE("SafeHttpServer::StartListening") << LOG_DESC("use default epoll");
#endif


        if (ipv6Enabled)
        {
            struct sockaddr_in6 sock;
            sock.sin6_family = AF_INET6;
            sock.sin6_port = htons(this->port);
            auto ret = inet_pton(AF_INET6, m_address.c_str(), &sock.sin6_addr);
            if (ret != 1)
            {
                LOG(ERROR) << LOG_BADGE("SafeHttpServer parse ipv6 failed");
                return false;
            }

            if (this->path_sslcert != "" && this->path_sslkey != "")
            {
                this->daemon = MHD_start_daemon(MHD_USE_DUAL_STACK | MHD_USE_SSL | ioMultiDevice,
                    this->port, NULL, NULL, SafeHttpServer::callback, this, MHD_OPTION_SOCK_ADDR,
                    &sock, MHD_OPTION_HTTPS_MEM_KEY, this->sslkey.c_str(),
                    MHD_OPTION_HTTPS_MEM_CERT, this->sslcert.c_str(), MHD_OPTION_THREAD_POOL_SIZE,
                    this->threads, MHD_OPTION_CONNECTION_LIMIT, 1000000, MHD_OPTION_END);
            }
            else
            {
                this->daemon = MHD_start_daemon(
                    ioMultiDevice | MHD_USE_DUAL_STACK | MHD_FEATURE_MESSAGES, this->port, NULL,
                    NULL, SafeHttpServer::callback, this, MHD_OPTION_SOCK_ADDR, &sock,
                    MHD_OPTION_THREAD_POOL_SIZE, this->threads, MHD_OPTION_END);
            }
            LOG(INFO) << LOG_BADGE("SafeHttpServer use ipv6") << LOG_KV("address", m_address)
                      << LOG_KV("ipv6 supported", this->daemon ? "true" : "false");
        }
        else
        {  // ipv4
            struct sockaddr_in sock;
            sock.sin_family = AF_INET;
            sock.sin_port = htons(this->port);
            sock.sin_addr.s_addr = inet_addr(m_address.c_str());
            if (this->path_sslcert != "" && this->path_sslkey != "")
            {
                this->daemon = MHD_start_daemon(MHD_USE_SSL | ioMultiDevice, this->port, NULL, NULL,
                    SafeHttpServer::callback, this, MHD_OPTION_SOCK_ADDR, &sock,
                    MHD_OPTION_HTTPS_MEM_KEY, this->sslkey.c_str(), MHD_OPTION_HTTPS_MEM_CERT,
                    this->sslcert.c_str(), MHD_OPTION_THREAD_POOL_SIZE, this->threads,
                    MHD_OPTION_CONNECTION_LIMIT, 1000000, MHD_OPTION_END);
            }
            else
            {
                this->daemon = MHD_start_daemon(ioMultiDevice, this->port, NULL, NULL,
                    SafeHttpServer::callback, this, MHD_OPTION_SOCK_ADDR, &sock,
                    MHD_OPTION_THREAD_POOL_SIZE, this->threads, MHD_OPTION_END);
            }
        }
    }
    if (this->daemon != NULL)
    {
        this->running = true;
    }
    return this->running;
}

bool SafeHttpServer::StopListening()
{
    if (this->running)
    {
        MHD_stop_daemon(this->daemon);
        this->running = false;
    }
    return true;
}


bool SafeHttpServer::SendResponse(string const& _response, void* _addInfo)
{
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
    struct MHD_Response* result = MHD_create_response_from_buffer(_response.size(),
        static_cast<void*>(const_cast<char*>(_response.c_str())), MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Content-Type", "application/json");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", m_allowedOrigin.c_str());

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

bool SafeHttpServer::SendOptionsResponse(void* _addInfo)
{
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(_addInfo);
    struct MHD_Response* result =
        MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Allow", "POST, OPTIONS");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", m_allowedOrigin.c_str());
    MHD_add_response_header(result, "Access-Control-Allow-Headers", "origin, content-type, accept");
    MHD_add_response_header(result, "DAV", "1");

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

void SafeHttpServer::SetUrlHandler(const string& url, jsonrpc::IClientConnectionHandler* handler)
{
    this->urlhandler[url] = handler;
    this->SetHandler(NULL);
}

int SafeHttpServer::callback(void* cls, MHD_Connection* connection, const char* url,
    const char* method, const char* version, const char* upload_data, size_t* upload_data_size,
    void** con_cls)
{
    (void)version;
    if (*con_cls == NULL)
    {
        struct mhd_coninfo* client_connection = new mhd_coninfo;
        client_connection->connection = connection;
        client_connection->server = static_cast<SafeHttpServer*>(cls);
        *con_cls = client_connection;
        return MHD_YES;
    }
    struct mhd_coninfo* client_connection = static_cast<struct mhd_coninfo*>(*con_cls);

    if (string("POST") == method)
    {
        if (*upload_data_size != 0)
        {
            client_connection->request.write(upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }
        else
        {
            string response;
            jsonrpc::IClientConnectionHandler* handler =
                client_connection->server->GetHandler(string(url));
            if (handler == NULL)
            {
                client_connection->code = MHD_HTTP_INTERNAL_SERVER_ERROR;
                client_connection->server->SendResponse(
                    "No client conneciton handler found", client_connection);
            }
            else
            {
                client_connection->code = MHD_HTTP_OK;
                try
                {
                    handler->HandleRequest(client_connection->request.str(), response);
                }
                catch (std::exception& e)
                {
                    client_connection->server->SendResponse(
                        std::string("ERROR while handleRequest:") + e.what(), client_connection);

                    return MHD_YES;
                }
                client_connection->server->SendResponse(response, client_connection);
            }
        }
    }
    else if (string("OPTIONS") == method)
    {
        client_connection->code = MHD_HTTP_OK;
        client_connection->server->SendOptionsResponse(client_connection);
    }
    else
    {
        client_connection->code = MHD_HTTP_METHOD_NOT_ALLOWED;
        client_connection->server->SendResponse("Not allowed HTTP Method", client_connection);
    }
    delete client_connection;
    *con_cls = NULL;

    return MHD_YES;
}
