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
 * @file: RateLimitHttpServer.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef JSONRPC_CPP_RATE_LIMIT_HTTPSERVERCONNECTOR_H_
#define JSONRPC_CPP_RATE_LIMIT_HTTPSERVERCONNECTOR_H_

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <ws2tcpip.h>
#if defined(_MSC_FULL_VER) && !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif // !_SSIZE_T_DEFINED */
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#endif
#include <sys/select.h>
#include <map>
#include <microhttpd.h>
#include "jsonrpccpp/server/abstractserverconnector.h"
#include "RateLimiter.h"
#ifndef KEY_REQUEST_METHODNAME
#define KEY_REQUEST_METHODNAME "method"
#endif

typedef int (*pCallBack)(void *, struct MHD_Connection *, const char *, const char *, const char *, const char *, size_t *, void **);
typedef void (*pCompletedCallbak)(void *, struct MHD_Connection *,void **, enum MHD_RequestTerminationCode);

namespace jsonrpc
{
/**
     * This class provides an embedded HTTP Server, based on libmicrohttpd, to handle incoming Requests and send HTTP 1.1
     * valid responses.
     * Note that this class will always send HTTP-Status 200, even though an JSON-RPC Error might have occurred. Please
     * always check for the JSON-RPC Error Header.
     */
class HttpServer : public AbstractServerConnector
{
public:
    /**
             * @brief HttpServer, constructor for the included HttpServer
             * @param port on which the server is listening
             * @param enableSpecification - defines if the specification is returned in case of a GET request
             * @param sslcert - defines the path to a SSL certificate, if this path is != "", then SSL/HTTPS is used with the given certificate.
             */
    HttpServer(int port, const std::string &sslcert = "", const std::string &sslkey = "", int threads = 50, const std::string &config = "");

    virtual bool StartListening();
    virtual bool StopListening();

    bool virtual SendResponse(const std::string &response,
                              void *addInfo = NULL);
    bool virtual SendOptionsResponse(void *addInfo);

    void SetUrlHandler(const std::string &url, IClientConnectionHandler *handler);

    bool isRunning() const {return running;}

    virtual pCallBack getCallback() {
        return HttpServer::callback;
    }
    virtual pCompletedCallbak getCompletedCallback() {
        return HttpServer::reqeuest_completed; // do nothing by default
    }
private:
    int port;
    int threads;
    bool running;
    std::string path_sslcert;
    std::string path_sslkey;
    std::string sslcert;
    std::string sslkey;
    dev::RateLimiter limiter;
    struct MHD_Daemon *daemon;
    std::map<std::string, IClientConnectionHandler *> urlhandler;
    bool checkPermit(void *client_connection, std::string &response);

protected:
    static int callback(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls);
    static void reqeuest_completed(void *cls,
        struct MHD_Connection *connection,
        void **con_cls,
        enum MHD_RequestTerminationCode toe) {}

    IClientConnectionHandler *GetHandler(const std::string &url);
};

} /* namespace jsonrpc */
#endif /* JSONRPC_CPP_RATE_LIMIT_HTTPSERVERCONNECTOR_H_ */
