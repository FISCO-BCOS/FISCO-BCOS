/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file Config.cpp
 * @author: octopus
 * @date 2021-12-14
 */
#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/interfaces/NodeInfoDef.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <bcos-cpp-sdk/config/Config.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/exception/diagnostic_information.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::context;
using namespace bcos;
using namespace bcos::cppsdk::config;

std::shared_ptr<bcos::boostssl::ws::WsConfig> Config::loadConfig(const std::string& _configPath)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(_configPath, pt);

        BCOS_LOG(INFO) << LOG_BADGE("loadConfig") << LOG_DESC("loadConfig ok")
                       << LOG_KV("configPath", _configPath);
        return loadConfig(pt);
    }
    catch (const std::exception& e)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("loadConfig") << LOG_DESC("loadConfig failed")
                          << LOG_KV("configPath", _configPath)
                          << LOG_KV("error", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(boost::diagnostic_information(e)));
    }
}

std::shared_ptr<bcos::boostssl::ws::WsConfig> Config::loadConfig(
    boost::property_tree::ptree const& _pt)
{
    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Client);

    loadCommon(_pt, *config);
    loadPeers(_pt, *config);
    if (!config->disableSsl())
    {
        loadCert(_pt, *config);
    }

    return config;
}

void Config::loadCommon(
    boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config)
{
    /*
    [common]
        ; if disable ssl connection, default: false
        ; disable_ssl = true
        ; thread pool size for network msg sending recving handing
        thread_pool_size = 8
        ; send message timeout(ms)
        message_timeout_ms = 10000
    */
    bool disableSsl = _pt.get<bool>("common.disable_ssl", false);
    int threadPoolSize = _pt.get<int>("common.thread_pool_size", 8);
    int messageTimeOut = _pt.get<int>("common.message_timeout_ms", 10000);

    _config.setDisableSsl(disableSsl);
    _config.setSendMsgTimeout(messageTimeOut);
    _config.setThreadPoolSize(threadPoolSize);


    BCOS_LOG(INFO) << LOG_BADGE("loadCommon") << LOG_DESC("load common section config items ok")
                   << LOG_KV("disableSsl", disableSsl) << LOG_KV("threadPoolSize", threadPoolSize)
                   << LOG_KV("messageTimeOut", messageTimeOut);
}

void Config::loadPeers(
    boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config)
{
    /*
    [peers]
    # supported ipv4 and ipv6
        node.0=127.0.0.1:20200
        node.1=127.0.0.1:20201
    */

    EndPointsPtr peers = std::make_shared<std::set<NodeIPEndpoint>>();
    _config.setConnectPeers(peers);

    for (auto it : _pt.get_child("peers"))
    {
        if (it.first.find("node.") != 0)
        {
            continue;
        }

        NodeIPEndpoint ep;
        if (!WsTools::stringToEndPoint(it.second.data(), ep))
        {
            BCOS_LOG(WARNING)
                << LOG_BADGE("loadPeers")
                << LOG_DESC(
                       "invalid endpoint, endpoint must be in IP:Port format, eg: 127.0.0.1:20200")
                << LOG_KV("endpoint", it.second.data());
            continue;
        }

        BCOS_LOG(INFO) << LOG_BADGE("loadPeers") << LOG_DESC("add one connected peer")
                       << LOG_KV("endpoint", it.second.data());

        peers->insert(ep);
    }

    BCOS_LOG(DEBUG) << LOG_BADGE("loadPeers") << LOG_DESC("load connected peers ok")
                    << LOG_KV("peers size", peers->size());
}

void Config::loadCert(boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config)
{
    std::string sslType = _pt.get<std::string>("cert.ssl_type", "ssl");
    if (sslType == "sm_ssl")
    {
        loadSMSslCert(_pt, _config);
    }
    else
    {
        loadSslCert(_pt, _config);
    }
}

void Config::loadSslCert(
    boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config)
{
    /*
    [cert]
        ; directory the certificates located in, default: ./conf
        ca_path=./conf
        ; the ca certificate file
        ca_cert=ca.crt
        ; the node private key file
        sdk_key=sdk.key
        ; the node certificate file
        sdk_cert=sdk.crt
   */
    std::string caPath = _pt.get<std::string>("cert.ca_path", "./conf");
    std::string caCert = caPath + "/" + _pt.get<std::string>("cert.ca_cert", "ca.crt");
    std::string sdkKey = caPath + "/" + _pt.get<std::string>("cert.sdk_key", "sdk.key");
    std::string sdkCert = caPath + "/" + _pt.get<std::string>("cert.sdk_cert", "sdk.crt");

    BCOS_LOG(INFO) << LOG_BADGE("loadCert") << LOG_DESC("load ssl cert ok")
                   << LOG_KV("ca_path", caPath) << LOG_KV("caCert", caCert)
                   << LOG_KV("sdkCert", sdkCert) << LOG_KV("sdkKey", sdkKey);

    ContextConfig::CertConfig certConfig;
    certConfig.caCert = caCert;
    certConfig.nodeCert = sdkCert;
    certConfig.nodeKey = sdkKey;

    auto contextConfig = std::make_shared<ContextConfig>();
    contextConfig->setSslType("ssl");
    contextConfig->setCertConfig(certConfig);
    _config.setContextConfig(contextConfig);
    return;
}

void Config::loadSMSslCert(
    boost::property_tree::ptree const& _pt, bcos::boostssl::ws::WsConfig& _config)
{
    /*
   [cert]
        ; directory the certificates located in, default: ./conf
        ca_path=./conf
        ; the ca certificate file
        sm_ca_cert=sm_ca.crt
        ; the node private key file
        sm_sdk_key=sm_sdk.key
        ; the node certificate file
        sm_sdk_cert=sm_sdk.crt
        ; the node private key file
        sm_ensdk_key=sm_ensdk.key
        ; the node certificate file
        sm_ensdk_cert=sm_ensdk.crt
   */
    std::string caPath = _pt.get<std::string>("cert.ca_path", "./conf");
    std::string smCaCert = caPath + "/" + _pt.get<std::string>("cert.sm_ca_cert", "sm_ca.crt");
    std::string smSdkKey = caPath + "/" + _pt.get<std::string>("cert.sm_sdk_key", "sm_sdk.key");
    std::string smSdkCert = caPath + "/" + _pt.get<std::string>("cert.sm_sdk_cert", "sm_sdk.crt");
    std::string smEnSdkKey =
        caPath + "/" + _pt.get<std::string>("cert.sm_ensdk_key", "sm_ensdk.key");
    std::string smEnSdkCert =
        caPath + "/" + _pt.get<std::string>("cert.sm_ensdk_cert", "sm_ensdk.crt");

    BCOS_LOG(INFO) << LOG_DESC("loadSMCert") << LOG_DESC("load sm ssl cert ok")
                   << LOG_KV("ca_path", caPath) << LOG_KV("smCaCert", smCaCert)
                   << LOG_KV("smSdkKey", smSdkKey) << LOG_KV("smSdkCert", smSdkCert)
                   << LOG_KV("smEnSdkCert", smEnSdkCert) << LOG_KV("smEnSdkKey", smEnSdkKey);


    ContextConfig::SMCertConfig cert;
    cert.caCert = smCaCert;
    cert.nodeCert = smSdkCert;
    cert.nodeKey = smSdkKey;
    cert.enNodeCert = smEnSdkCert;
    cert.enNodeKey = smEnSdkKey;

    auto ctxConfig = std::make_shared<ContextConfig>();
    ctxConfig->setSslType("sm_ssl");
    ctxConfig->setSmCertConfig(cert);
    _config.setContextConfig(ctxConfig);
    return;
}
