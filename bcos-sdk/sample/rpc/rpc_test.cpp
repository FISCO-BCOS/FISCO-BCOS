/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file rpc.cpp
 * @author: kyonGuo
 * @date 2024/10/19
 */
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/Sdk.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-utilities/BoostLog.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void usage(void)
{
    printf("Desc: rpc methods call test\n");
    printf("Usage: rpc <host> <port> <ssl type> <group_id>\n");
    printf("Example:\n");
    printf("   ./rpc 127.0.0.1 20200 ssl group0\n");
    printf("   ./rpc 127.0.0.1 20200 sm_ssl group0\n");
    exit(0);
}
struct bcos_sdk_c_endpoint
{
    std::string host;  // c-style string，end with '\0', support: ipv4 / ipv6 / domain name
    uint16_t port;
};
struct bcos_sdk_c_cert_config
{
    std::string ca_cert;  // cert and key should be in pem format
    std::string node_key;
    std::string node_cert;
};
struct bcos_sdk_c_config
{
    // common config
    int thread_pool_size;
    int message_timeout_ms;
    int reconnect_period_ms;
    int heartbeat_period_ms;
    // enable send rpc request to the highest block number node, default: true
    int send_rpc_request_to_highest_block_node;
    // connected peers
    std::vector<bcos_sdk_c_endpoint> peers;
    size_t peers_count;
    // the switch for disable ssl connection
    int disable_ssl;
    // ssl or sm_ssl
    std::string ssl_type;
    // cert config items is the content of the cert or the path of the cert file
    int is_cert_path;
    // ssl connection cert, effective with ssl_type is 'ssl'
    bcos_sdk_c_cert_config cert_config;
};
static std::shared_ptr<bcos::boostssl::ws::WsConfig> initWsConfig(
    std::shared_ptr<bcos_sdk_c_config> config)
{
    // TODO: check params
    // init WsConfig by c Config
    auto wsConfig = std::make_shared<bcos::boostssl::ws::WsConfig>();
    wsConfig->setModel(bcos::boostssl::ws::WsModel::Client);
    auto peers = std::make_shared<bcos::boostssl::ws::EndPoints>();
    for (size_t i = 0; i < config->peers_count; i++)
    {
        auto host = config->peers[i].host;
        auto port = config->peers[i].port;
        bcos::boostssl::NodeIPEndpoint endpoint = bcos::boostssl::NodeIPEndpoint(host, port);
        peers->insert(endpoint);
    }
    wsConfig->setConnectPeers(peers);
    wsConfig->setDisableSsl(config->disable_ssl);
    wsConfig->setThreadPoolSize(config->thread_pool_size);
    wsConfig->setReconnectPeriod(config->reconnect_period_ms);
    wsConfig->setHeartbeatPeriod(config->heartbeat_period_ms);
    wsConfig->setSendMsgTimeout(config->message_timeout_ms);
    if (!config->disable_ssl)
    {
        auto contextConfig = std::make_shared<bcos::boostssl::context::ContextConfig>();
        contextConfig->setSslType(config->ssl_type);
        contextConfig->setIsCertPath(config->is_cert_path ? true : false);
        bcos::boostssl::context::ContextConfig::CertConfig certConfig;
        certConfig.caCert = config->cert_config.ca_cert;
        certConfig.nodeCert = config->cert_config.node_cert;
        certConfig.nodeKey = config->cert_config.node_key;
        contextConfig->setCertConfig(certConfig);
        wsConfig->setContextConfig(contextConfig);
    }
    return wsConfig;
}
void* thread_function(std::shared_ptr<bcos_sdk_c_config> arg)
{
    while (1)
    {
        auto factory = std::make_shared<bcos::cppsdk::SdkFactory>();
        auto wsConfig = initWsConfig(arg);
        auto sdk = factory->buildSdk(wsConfig, arg->send_rpc_request_to_highest_block_node);
        sdk->start();
        auto rpc = sdk->jsonRpc();
        rpc->getBlockNumber(
            "group0", "", [](bcos::Error::Ptr error, std::shared_ptr<bcos::bytes> resp) {});
        usleep(100);
        sdk->stop();
        sdk.reset(nullptr);
    }
}
std::shared_ptr<bcos_sdk_c_config> bcos_sdk_create_config(
    int sm_ssl, std::string host, uint16_t port)
{
    // create c-sdk config object
    auto config = std::make_shared<bcos_sdk_c_config>();
    config->thread_pool_size = 0;
    config->message_timeout_ms = 0;
    config->heartbeat_period_ms = 0;
    config->reconnect_period_ms = 0;
    config->disable_ssl = 0;
    config->send_rpc_request_to_highest_block_node = 1;
    config->peers = {};
    config->peers_count = 0;
    // set thread pool size
    config->thread_pool_size = 8;
    // set message timeout(unit: ms)
    config->message_timeout_ms = 10000;
    // --- set connected peers ---------
    struct bcos_sdk_c_endpoint ep = {host, port};
    config->peers.push_back(ep);
    config->peers_count = 1;
    // --- set connected peers ---------
    config->disable_ssl = 1;
    config->send_rpc_request_to_highest_block_node = 1;
    // set ssl type
    config->ssl_type = sm_ssl ? "sm_ssl" : "ssl";
    // --- set ssl cert ---------
    // cert config items is the path of file ,not the content
    config->is_cert_path = 1;
    config->cert_config = {
        .ca_cert = "./conf/ca.crt", .node_key = "./conf/sdk.key", .node_cert = "./conf/sdk.crt"};
    // --- set ssl cert ---------
    return config;
}
int main(int argc, char** argv)
{
    if (argc < 5)
    {
        usage();
    }
    // option params
    const char* host = argv[1];
    int port = atoi(argv[2]);
    const char* type = argv[3];
    const char* group = argv[4];
    printf(" [RPC] params ===>>>> \n");
    printf(" \t # host: %s\n", host);
    printf(" \t # port: %d\n", port);
    printf(" \t # type: %s\n", type);
    printf(" \t # group: %s\n", group);
    int is_sm_ssl = 1;
    char const* pos = strstr(type, "sm_ssl");
    if (pos == NULL)
    {
        is_sm_ssl = 0;
    }
    auto config = bcos_sdk_create_config(is_sm_ssl, (char*)host, port);
    config->disable_ssl = 1;
    // check success or not
    std::thread threads[100];
    int rc;
    long t;
    for (t = 0; t < 100; t++)
    {
        printf("In main: creating thread %ld\n", t);
        threads[t] = std::thread(thread_function, config);
        //        rc = pthread_create(&threads[t], NULL, thread_function);
        //        if (rc)
        //        {
        //            printf("ERROR; return code from pthread_create() is %d\n", rc);
        //            exit(-1);
        //        }
    }
    for (t = 0; t < 100; t++)
    {
        threads[t].join();
    }
    return EXIT_SUCCESS;
}