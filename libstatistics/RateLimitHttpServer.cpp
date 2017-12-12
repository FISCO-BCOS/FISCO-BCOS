/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: RateLimitHttpServer.cpp
 * @author: fisco-dev
 *
 * @date: 2017
 */

#include <cstdlib>
#include <sstream>
#include <iostream>
#include <jsonrpccpp/common/specificationparser.h>
#include <arpa/inet.h>
#include "RateLimitHttpServer.h"
#include <libdevcore/easylog.h>

using namespace jsonrpc;
using namespace std;

#define BUFFERSIZE 65536

struct mhd_coninfo
{
    struct MHD_PostProcessor *postprocessor;
    MHD_Connection *connection;
    stringstream request;
    HttpServer *server;
    int code;
};

HttpServer::HttpServer(int port, const std::string &sslcert,
                       const std::string &sslkey, int threads, const std::string &config) : AbstractServerConnector(),
    port(port), threads(threads), running(false), path_sslcert(sslcert),
    path_sslkey(sslkey), limiter(config), daemon(NULL)
{
}

IClientConnectionHandler *HttpServer::GetHandler(const std::string &url)
{
    if (AbstractServerConnector::GetHandler() != NULL)
        return AbstractServerConnector::GetHandler();
    map<string, IClientConnectionHandler *>::iterator it = this->urlhandler.find(url);
    if (it != this->urlhandler.end())
        return it->second;
    return NULL;
}

bool HttpServer::StartListening()
{
    if (!this->running)
    {
        unsigned int mhd_flags = MHD_USE_SELECT_INTERNALLY;

#if (MHD_VERSION >= 0x00095100)
        if (MHD_is_feature_supported(MHD_FEATURE_EPOLL) == MHD_YES)
            mhd_flags = MHD_USE_EPOLL_INTERNALLY;
        else if (MHD_is_feature_supported(MHD_FEATURE_POLL) == MHD_YES)
            mhd_flags = MHD_USE_POLL_INTERNALLY;
#endif
        if (this->path_sslcert != "" && this->path_sslkey != "")
        {
            try
            {
                SpecificationParser::GetFileContent(this->path_sslcert, this->sslcert);
                SpecificationParser::GetFileContent(this->path_sslkey, this->sslkey);

                this->daemon = MHD_start_daemon(MHD_USE_SSL | mhd_flags,
                                                this->port, NULL, NULL, getCallback(), this,
                                                MHD_OPTION_HTTPS_MEM_KEY, this->sslkey.c_str(),
                                                MHD_OPTION_HTTPS_MEM_CERT, this->sslcert.c_str(),
                                                MHD_OPTION_THREAD_POOL_SIZE, this->threads,
                                                MHD_OPTION_NOTIFY_COMPLETED, getCompletedCallback(), NULL,
                                                MHD_OPTION_END);
            }
            catch (JsonRpcException &ex)
            {
                return false;
            }
        }
        else
        {
            this->daemon = MHD_start_daemon(mhd_flags, this->port, NULL, NULL,
                                            getCallback(), this,
                                            MHD_OPTION_THREAD_POOL_SIZE, this->threads,
                                            MHD_OPTION_NOTIFY_COMPLETED, getCompletedCallback(), NULL,
                                            MHD_OPTION_END);
        }
        if (this->daemon != NULL)
            this->running = true;
    }
    return this->running;
}

bool HttpServer::StopListening()
{
    if (this->running)
    {
        MHD_stop_daemon(this->daemon);
        this->running = false;
    }
    return true;
}

bool HttpServer::SendResponse(const string &response, void *addInfo)
{
    struct mhd_coninfo *client_connection = static_cast<struct mhd_coninfo *>(addInfo);
    struct MHD_Response *result = MHD_create_response_from_buffer(response.size(), (void *)response.c_str(), MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Content-Type", "application/json");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", "*");

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

bool HttpServer::SendOptionsResponse(void *addInfo)
{
    struct mhd_coninfo *client_connection = static_cast<struct mhd_coninfo *>(addInfo);
    struct MHD_Response *result = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(result, "Allow", "POST, OPTIONS");
    MHD_add_response_header(result, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(result, "Access-Control-Allow-Headers", "origin, content-type, accept");
    MHD_add_response_header(result, "DAV", "1");

    int ret = MHD_queue_response(client_connection->connection, client_connection->code, result);
    MHD_destroy_response(result);
    return ret == MHD_YES;
}

void HttpServer::SetUrlHandler(const string &url, IClientConnectionHandler *handler)
{
    this->urlhandler[url] = handler;
    this->SetHandler(NULL);
}

int HttpServer::callback(void *cls, MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    (void)version;
    if (*con_cls == NULL)
    {
        struct mhd_coninfo *client_connection = new mhd_coninfo;
        client_connection->connection = connection;
        client_connection->server = static_cast<HttpServer *>(cls);
        *con_cls = client_connection;
        return MHD_YES;
    }
    struct mhd_coninfo *client_connection = static_cast<struct mhd_coninfo *>(*con_cls);

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
            IClientConnectionHandler *handler = client_connection->server->GetHandler(string(url));
            if (handler == NULL)
            {
                client_connection->code = MHD_HTTP_INTERNAL_SERVER_ERROR;
                client_connection->server->SendResponse("No client connection handler found", client_connection);
            }
            else
            {

                client_connection->code = MHD_HTTP_OK;
                if (client_connection->server->checkPermit(client_connection, response))
                    handler->HandleRequest(client_connection->request.str(), response);
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

bool HttpServer::checkPermit(void *client_conn, string &response)
{
    struct mhd_coninfo *client_connection = static_cast<struct mhd_coninfo *>(client_conn);
    const MHD_ConnectionInfo *info = MHD_get_connection_info(client_connection->connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    struct sockaddr_in *client = (struct sockaddr_in *)(info->client_addr);
    string ip(16, '0');
    inet_ntop(AF_INET, &client->sin_addr, &ip[0], ip.size());

    Json::Reader reader;
    Json::Value req;
    Json::Value resp;
    Json::FastWriter w;
    bool permit = true;

    if (reader.parse(client_connection->request.str(), req, false))
    {
        if (req.isArray() && req.size() > 0)
        {
            for (unsigned int i = 0; i < req.size(); i++)
            {
                if (!limiter.isPermitted(ip, req[i][KEY_REQUEST_METHODNAME].asString()))
                {
                    permit = false;
                    break;
                }
            }
        }
        else if (req.isObject() && !limiter.isPermitted(ip, req[KEY_REQUEST_METHODNAME].asString()))
            permit = false;
        if (!permit)
        {
            resp["jsonrpc"] = "2.0";
            resp["error"]["code"] = Errors::ERROR_RPC_INVALID_REQUEST;
            resp["error"]["message"] = "INVALID_REQUEST: The operation frequency is too fast. Please try again later";
            resp["id"] = req["id"];
            response = w.write(resp);
        }
    }

    return permit;
}