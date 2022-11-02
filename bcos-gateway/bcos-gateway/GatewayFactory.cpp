/** @file GatewayFactory.cpp
 *  @author octopus
 *  @date 2021-05-17
 */

#include "bcos-gateway/libratelimit/DistributedRateLimiter.h"
#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-boostssl/context/Common.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/gateway/GatewayMessageExtAttributes.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/gateway/ProGatewayNodeManager.h>
#include <bcos-gateway/libamop/AirTopicManager.h>
#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libp2p/P2PMessageV2.h>
#include <bcos-gateway/libp2p/ServiceV2.h>
#include <bcos-gateway/libp2p/router/RouterTableImpl.h>
#include <bcos-gateway/libratelimit/GatewayRateLimiter.h>
#include <bcos-gateway/libratelimit/RateLimiterManager.h>
#include <bcos-gateway/libratelimit/TokenBucketRateLimiter.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>

using namespace bcos::rpc;
using namespace bcos;
using namespace security;
using namespace gateway;
using namespace bcos::amop;
using namespace bcos::protocol;
using namespace bcos::boostssl;

// register the function fetch pub hex from the cert
void GatewayFactory::initCert2PubHexHandler()
{
    auto handler = [](const std::string& _cert, std::string& _pubHex) -> bool {
        std::string errorMessage;
        do
        {
            auto certContent = readContentsToString(boost::filesystem::path(_cert));
            if (!certContent || certContent->empty())
            {
                errorMessage = "unable to load cert content, cert: " + _cert;
                break;
            }

            GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                                      << LOG_KV("certContent: ", certContent);

            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [](BIO* p) {
                if (p != NULL)
                {
                    BIO_free(p);
                }
            });

            if (!bioMem)
            {
                errorMessage = "BIO_new error";
                break;
            }

            BIO_write(bioMem.get(), certContent->data(), certContent->size());
            std::shared_ptr<X509> x509Ptr(
                PEM_read_bio_X509(bioMem.get(), NULL, NULL, NULL), [](X509* p) {
                    if (p != NULL)
                    {
                        X509_free(p);
                    }
                });

            if (!x509Ptr)
            {
                errorMessage = "PEM_read_bio_X509 error";
                break;
            }

            ASN1_BIT_STRING* pubKey = X509_get0_pubkey_bitstr(x509Ptr.get());
            if (!pubKey)
            {
                errorMessage = "X509_get0_pubkey_bitstr error";
                break;
            }

            auto hex = bcos::toHexString(pubKey->data, pubKey->data + pubKey->length, "");
            _pubHex = *hex.get();

            GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("initCert2PubHexHandler ")
                                      << LOG_KV("cert", _cert) << LOG_KV("pubHex: ", _pubHex);
            return true;
        } while (0);

        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                                   << LOG_KV("errorMessage", errorMessage);
        return false;
    };

    m_certPubHexHandler = handler;
}

// register the function fetch public key from the ssl context
void GatewayFactory::initSSLContextPubHexHandler()
{
    auto handler = [](X509* x509, std::string& _pubHex) -> bool {
        ASN1_BIT_STRING* pubKey =
            X509_get0_pubkey_bitstr(x509);  // csc->current_cert is an X509 struct
        if (pubKey == NULL)
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initSSLContextPubHexHandler X509_get0_pubkey_bitstr error");
            return false;
        }

        auto hex = bcos::toHexString(pubKey->data, pubKey->data + pubKey->length, "");
        _pubHex = *hex.get();

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("[NEW]SSLContext pubHex: " + _pubHex);
        return true;
    };

    m_sslContextPubHandler = handler;
}

std::shared_ptr<boost::asio::ssl::context> GatewayFactory::buildSSLContext(
    bool _server, const GatewayConfig::CertConfig& _certConfig)
{
    std::ignore = _server;
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
    /*
      std::shared_ptr<EC_KEY> ecdh(EC_KEY_new_by_curve_name(NID_secp384r1),
                                   [](EC_KEY *p) { EC_KEY_free(p); });
      SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
   */
    std::shared_ptr<bytes> keyContent;
    if (!_certConfig.nodeKey.empty())
    {
        try
        {
            if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                keyContent = readContents(boost::filesystem::path(_certConfig.nodeKey));
            else
                keyContent = m_dataEncrypt->decryptFile(_certConfig.nodeKey);
        }
        catch (std::exception& e)
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                << LOG_KV("file", _certConfig.nodeKey);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "buildSSLContext: unable read content of key: " + _certConfig.nodeKey));
        }
    }
    if (!keyContent || keyContent->empty())
    {
        GATEWAY_FACTORY_LOG(ERROR)
            << LOG_DESC("buildSSLContext: unable read content of key: " + _certConfig.nodeKey);
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "buildSSLContext: unable read content of key: " + _certConfig.nodeKey));
    }

    boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    // node.crt
    sslContext->use_certificate_chain_file(_certConfig.nodeCert);
    /*if (!SSL_CTX_get0_certificate(sslContext->native_handle())) {
      GATEWAY_FACTORY_LOG(ERROR)
          << LOG_DESC("buildSSLContext: SSL_CTX_get0_certificate failed");
      BOOST_THROW_EXCEPTION(
          InvalidParameter() << errinfo_comment(
              "buildSSLContext: SSL_CTX_get0_certificate failed, node_cert=" +
              _certConfig.nodeCert));
    }*/

    auto caCertContent =
        readContentsToString(boost::filesystem::path(_certConfig.caCert));  // ca.crt
    if (!caCertContent || caCertContent->empty())
    {
        GATEWAY_FACTORY_LOG(ERROR)
            << LOG_DESC("buildSSLContext: unable read content of ca: " + _certConfig.caCert);
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "buildSSLContext: unable read content of ca: " + _certConfig.caCert));
    }
    sslContext->add_certificate_authority(
        boost::asio::const_buffer(caCertContent->data(), caCertContent->size()));

    std::string caPath;
    if (!caPath.empty())
    {
        sslContext->add_verify_path(caPath);
    }

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);

    return sslContext;
}

std::shared_ptr<boost::asio::ssl::context> GatewayFactory::buildSSLContext(
    bool _server, const GatewayConfig::SMCertConfig& _smCertConfig)
{
    SSL_CTX* ctx = NULL;
    if (_server)
    {
        const SSL_METHOD* meth = SSLv23_server_method();
        ctx = SSL_CTX_new(meth);
    }
    else
    {
        const SSL_METHOD* meth = CNTLS_client_method();
        ctx = SSL_CTX_new(meth);
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(ctx);

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);

    /* Load the server certificate into the SSL_CTX structure */
    if (SSL_CTX_use_certificate_file(
            sslContext->native_handle(), _smCertConfig.nodeCert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_certificate_file error"));
    }

    std::shared_ptr<bytes> keyContent;
    if (!_smCertConfig.nodeKey.empty())
    {
        try
        {
            if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                keyContent = readContents(boost::filesystem::path(_smCertConfig.nodeKey));
            else
                keyContent = m_dataEncrypt->decryptFile(_smCertConfig.nodeKey);
        }
        catch (std::exception& e)
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                << LOG_KV("file", _smCertConfig.nodeKey);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "buildSSLContext: unable read content of key: " + _smCertConfig.nodeKey));
        }
    }
    // nodekey
    boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    /* Check if the server certificate and private-key matches */
    if (!SSL_CTX_check_private_key(sslContext->native_handle()))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_private_key error"));
    }

    std::shared_ptr<bytes> enNodeKeyContent;
    if (!_smCertConfig.enNodeKey.empty())
    {
        try
        {
            if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                enNodeKeyContent = readContents(boost::filesystem::path(_smCertConfig.enNodeKey));
            else
                enNodeKeyContent = m_dataEncrypt->decryptFile(_smCertConfig.enNodeKey);
        }
        catch (std::exception& e)
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                << LOG_KV("file", _smCertConfig.enNodeKey);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "buildSSLContext: unable read content of key: " + _smCertConfig.enNodeKey));
        }
    }


    /* Load the server encrypt certificate into the SSL_CTX structure */
    if (SSL_CTX_use_enc_certificate_file(
            sslContext->native_handle(), _smCertConfig.enNodeCert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_certificate_file error"));
    }
    std::string enNodeKeyStr((const char*)enNodeKeyContent->data(), enNodeKeyContent->size());

    if (SSL_CTX_use_enc_PrivateKey(sslContext->native_handle(), toEvpPkey(enNodeKeyStr.c_str())) <=
        0)
    {
        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("SSL_CTX_use_enc_PrivateKey error");
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("GatewayFactory::buildSSLContext "
                                                  "SSL_CTX_use_enc_PrivateKey error"));
    }

    auto caContent =
        readContentsToString(boost::filesystem::path(_smCertConfig.caCert));  // node.key content

    sslContext->add_certificate_authority(
        boost::asio::const_buffer(caContent->data(), caContent->size()));

    std::string caPath;
    if (!caPath.empty())
    {
        sslContext->add_verify_path(caPath);
    }

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);

    return sslContext;
}

/**
 * @brief: construct Gateway
 * @param _configPath: config.ini path
 * @return void
 */
std::shared_ptr<Gateway> GatewayFactory::buildGateway(const std::string& _configPath,
    bool _airVersion, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint,
    std::string const& _gatewayServiceName)
{
    auto config = std::make_shared<GatewayConfig>();
    // load config
    if (_airVersion)
    {
        // the air mode not require the uuid(use p2pID as uuid by default)
        config->initConfig(_configPath, false);
    }
    else
    {
        // the pro mode require the uuid
        config->initConfig(_configPath, true);
    }
    config->loadP2pConnectedNodes();
    return buildGateway(config, _airVersion, _entryPoint, _gatewayServiceName);
}

std::shared_ptr<ratelimiter::GatewayRateLimiter> GatewayFactory::buildGatewayRateLimiter(
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
    const GatewayConfig::RedisConfig& _redisConfig)
{
    auto rateLimiterStat = std::make_shared<ratelimiter::RateLimiterStat>();
    rateLimiterStat->setStatInterval(_rateLimiterConfig.statInterval);

    // redis instance
    std::shared_ptr<sw::redis::Redis> redis = nullptr;
    if (_rateLimiterConfig.enableDistributedRatelimit)
    {  // init redis first
        redis = initRedis(_redisConfig);
    }

    auto rateLimiterManager = buildRateLimiterManager(_rateLimiterConfig, redis);

    auto gatewayRateLimiter =
        std::make_shared<ratelimiter::GatewayRateLimiter>(rateLimiterManager, rateLimiterStat);

    return gatewayRateLimiter;
}

std::shared_ptr<ratelimiter::RateLimiterManager> GatewayFactory::buildRateLimiterManager(
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
    std::shared_ptr<sw::redis::Redis> _redis)
{
    // rate limiter factory
    auto rateLimiterFactory = std::make_shared<ratelimiter::RateLimiterFactory>(_redis);
    // rate limiter manager
    auto rateLimiterManager = std::make_shared<ratelimiter::RateLimiterManager>(_rateLimiterConfig);

    // total outgoing bandwidth Limit for p2p network
    ratelimiter::RateLimiterInterface::Ptr totalOutgoingRateLimiter = nullptr;
    if (_rateLimiterConfig.totalOutgoingBwLimit > 0)
    {
        totalOutgoingRateLimiter = rateLimiterFactory->buildTokenBucketRateLimiter(
            _rateLimiterConfig.totalOutgoingBwLimit);

        rateLimiterManager->registerRateLimiter(
            ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY, totalOutgoingRateLimiter);
    }

    // ip connection => rate limit
    if (!_rateLimiterConfig.ip2BwLimit.empty())
    {
        for (const auto& [ip, bandWidth] : _rateLimiterConfig.ip2BwLimit)
        {
            auto rateLimiterInterface = rateLimiterFactory->buildTokenBucketRateLimiter(bandWidth);
            rateLimiterManager->registerRateLimiter(ip, rateLimiterInterface);
        }
    }

    // group => rate limit
    if (!_rateLimiterConfig.group2BwLimit.empty())
    {
        for (const auto& [group, bandWidth] : _rateLimiterConfig.group2BwLimit)
        {
            ratelimiter::RateLimiterInterface::Ptr rateLimiterInterface = nullptr;
            if (_rateLimiterConfig.enableDistributedRatelimit)
            {
                rateLimiterInterface = rateLimiterFactory->buildRedisDistributedRateLimiter(
                    rateLimiterFactory->toTokenKey(group), bandWidth, 1,
                    _rateLimiterConfig.enableDistributedRateLimitCache,
                    _rateLimiterConfig.distributedRateLimitCachePercent);
            }
            else
            {
                rateLimiterInterface = rateLimiterFactory->buildTokenBucketRateLimiter(bandWidth);
            }

            rateLimiterManager->registerRateLimiter(group, rateLimiterInterface);
        }
    }

    // modules without bandwidth limit
    rateLimiterManager->setModulesWithoutLimit(_rateLimiterConfig.modulesWithoutLimit);
    rateLimiterManager->setRateLimiterFactory(rateLimiterFactory);

    return rateLimiterManager;
}

/**
 * @brief: construct Gateway
 * @param _config: config parameter object
 * @return void
 */
// Note: _gatewayServiceName is used to check the validation of groupInfo when localRouter update
// groupInfo
std::shared_ptr<Gateway> GatewayFactory::buildGateway(GatewayConfig::Ptr _config, bool _airVersion,
    bcos::election::LeaderEntryPointInterface::Ptr _entryPoint,
    std::string const& _gatewayServiceName)
{
    try
    {
        std::string nodeCert =
            (_config->smSSL() ? _config->smCertConfig().nodeCert : _config->certConfig().nodeCert);
        std::string pubHex;
        if (!m_certPubHexHandler(nodeCert, pubHex))
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "GatewayFactory::init unable parse myself pub id"));
        }

        std::shared_ptr<ba::ssl::context> srvCtx =
            (_config->smSSL() ? buildSSLContext(true, _config->smCertConfig()) :
                                buildSSLContext(true, _config->certConfig()));

        std::shared_ptr<ba::ssl::context> clientCtx =
            (_config->smSSL() ? buildSSLContext(false, _config->smCertConfig()) :
                                buildSSLContext(false, _config->certConfig()));

        // init ASIOInterface
        auto asioInterface = std::make_shared<ASIOInterface>();
        asioInterface->setIOServicePool(std::make_shared<IOServicePool>());
        asioInterface->setSrvContext(srvCtx);
        asioInterface->setClientContext(clientCtx);
        asioInterface->setType(ASIOInterface::ASIO_TYPE::SSL);

        // Message Factory
        auto messageFactory = std::make_shared<P2PMessageFactoryV2>();
        // Session Factory
        auto sessionFactory = std::make_shared<SessionFactory>(pubHex);
        // KeyFactory
        auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
        // init Host
        auto host = std::make_shared<Host>(asioInterface, sessionFactory, messageFactory);

        host->setHostPort(_config->listenIP(), _config->listenPort());
        host->setThreadPool(std::make_shared<ThreadPool>("P2P", _config->threadPoolSize()));
        host->setSSLContextPubHandler(m_sslContextPubHandler);

        // init Service
        auto routerTableFactory = std::make_shared<RouterTableFactoryImpl>();
        auto service = std::make_shared<ServiceV2>(pubHex, routerTableFactory);
        service->setHost(host);
        service->setStaticNodes(_config->connectedNodes());

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("GatewayFactory::init")
                                  << LOG_KV("myself pub id", pubHex)
                                  << LOG_KV("rpcService", m_rpcServiceName)
                                  << LOG_KV("gatewayServiceName", _gatewayServiceName);
        service->setMessageFactory(messageFactory);
        service->setKeyFactory(keyFactory);

        auto gatewayRateLimiter =
            buildGatewayRateLimiter(_config->rateLimiterConfig(), _config->redisConfig());
        auto gatewayRateLimiterWeakPtr =
            std::weak_ptr<ratelimiter::GatewayRateLimiter>(gatewayRateLimiter);

        // init GatewayNodeManager
        GatewayNodeManager::Ptr gatewayNodeManager;
        AMOPImpl::Ptr amop;
        if (_airVersion)
        {
            gatewayNodeManager =
                std::make_shared<GatewayNodeManager>(_config->uuid(), pubHex, keyFactory, service);
            amop = buildLocalAMOP(service, pubHex);
        }
        else
        {
            // Note: no need to use nodeAliveDetector when enable failover
            if (_entryPoint)
            {
                gatewayNodeManager = std::make_shared<GatewayNodeManager>(
                    _config->uuid(), pubHex, keyFactory, service);
            }
            else
            {
                gatewayNodeManager = std::make_shared<ProGatewayNodeManager>(
                    _config->uuid(), pubHex, keyFactory, service);
            }
            amop = buildAMOP(service, pubHex);
        }
        // init Gateway
        auto gateway = std::make_shared<Gateway>(
            m_chainID, service, gatewayNodeManager, amop, gatewayRateLimiter, _gatewayServiceName);
        auto GatewayNodeManagerWeakPtr = std::weak_ptr<GatewayNodeManager>(gatewayNodeManager);
        // register disconnect handler
        service->registerDisconnectHandler(
            [GatewayNodeManagerWeakPtr](NetworkException e, P2PSession::Ptr p2pSession) {
                (void)e;
                auto gatewayNodeManager = GatewayNodeManagerWeakPtr.lock();
                if (gatewayNodeManager && p2pSession)
                {
                    gatewayNodeManager->onRemoveNodeIDs(p2pSession->p2pID());
                }
            });
        service->registerUnreachableHandler(
            [GatewayNodeManagerWeakPtr](std::string const& _unreachableNode) {
                auto nodeMgr = GatewayNodeManagerWeakPtr.lock();
                if (!nodeMgr)
                {
                    return;
                }
                nodeMgr->onRemoveNodeIDs(_unreachableNode);
            });

        service->setBeforeMessageHandler([gatewayRateLimiterWeakPtr](SessionFace::Ptr _session,
                                             Message::Ptr _msg, SessionCallbackFunc _callback) {
            auto gatewayRateLimiter = gatewayRateLimiterWeakPtr.lock();
            if (!gatewayRateLimiter)
            {
                return true;
            }

            GatewayMessageExtAttributes::Ptr msgExtAttributes = nullptr;
            if (_msg->extAttributes())
            {
                msgExtAttributes =
                    std::dynamic_pointer_cast<GatewayMessageExtAttributes>(_msg->extAttributes());
            }

            std::string groupID = msgExtAttributes ? msgExtAttributes->groupID() : std::string();
            uint16_t moduleID = msgExtAttributes ? msgExtAttributes->moduleID() : 0;
            std::string endpoint = _session->nodeIPEndpoint().address();
            uint64_t msgLength = _msg->length();

            // bandwidth limit check
            auto r = gatewayRateLimiter->checkOutGoing(endpoint, groupID, moduleID, msgLength);
            if (!r.first && _callback)
            {
                _callback(NetworkException(BandwidthOverFlow, r.second), Message::Ptr());
            }

            return r.first;
        });

        service->setOnMessageHandler(
            [gatewayRateLimiterWeakPtr](SessionFace::Ptr _session, Message::Ptr _message) {
                auto gatewayRateLimiter = gatewayRateLimiterWeakPtr.lock();
                if (!gatewayRateLimiter)
                {
                    return true;
                }

                auto endpoint = _session->nodeIPEndpoint().address();
                auto msgLength = _message->length();
                gatewayRateLimiter->checkInComing(endpoint, msgLength);

                return true;
            });

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("GatewayFactory::init ok");
        if (!_entryPoint)
        {
            return gateway;
        }
        initFailOver(gateway, _entryPoint);

        return gateway;
    }
    catch (const std::exception& e)
    {
        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("GatewayFactory::init")
                                   << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
}

void GatewayFactory::initFailOver(
    std::shared_ptr<Gateway> _gateWay, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint)
{
    auto groupInfoCodec = std::make_shared<bcostars::protocol::GroupInfoCodecImpl>();
    _entryPoint->addMemberChangeNotificationHandler(
        [_gateWay, groupInfoCodec](
            std::string const& _leaderKey, bcos::protocol::MemberInterface::Ptr _leader) {
            auto const& groupInfoStr = _leader->memberConfig();
            auto groupInfo = groupInfoCodec->deserialize(groupInfoStr);
            GATEWAY_FACTORY_LOG(INFO)
                << LOG_DESC("The leader entryPoint changed") << LOG_KV("key", _leaderKey)
                << LOG_KV("memberID", _leader->memberID()) << LOG_KV("modifyIndex", _leader->seq())
                << LOG_KV("groupID", groupInfo->groupID());
            _gateWay->asyncNotifyGroupInfo(groupInfo, [](Error::Ptr&& _error) {
                if (_error)
                {
                    GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("memberChangedNotification error")
                                              << LOG_KV("code", _error->errorCode())
                                              << LOG_KV("msg", _error->errorMessage());
                    return;
                }
                GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("memberChangedNotification success");
            });
        });

    _entryPoint->addMemberDeleteNotificationHandler(
        [_gateWay, groupInfoCodec](
            std::string const& _leaderKey, bcos::protocol::MemberInterface::Ptr _leader) {
            auto const& groupInfoStr = _leader->memberConfig();
            auto groupInfo = groupInfoCodec->deserialize(groupInfoStr);
            GATEWAY_FACTORY_LOG(INFO)
                << LOG_DESC("The leader entryPoint has been deleted") << LOG_KV("key", _leaderKey)
                << LOG_KV("memberID", _leader->memberID()) << LOG_KV("modifyIndex", _leader->seq())
                << LOG_KV("groupID", groupInfo->groupID());
            auto nodeInfos = groupInfo->nodeInfos();
            for (auto const& node : nodeInfos)
            {
                _gateWay->unregisterNode(groupInfo->groupID(), node.second->nodeID());
                GATEWAY_FACTORY_LOG(INFO)
                    << LOG_DESC("unregisterNode") << LOG_KV("group", groupInfo->groupID())
                    << LOG_KV("node", node.second->nodeID());
            }
        });
    GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("initFailOver for gateway success");
}

/**
 * @brief
 *
 * @param _redisConfig
 * @return std::shared_ptr<sw::redis::Redis>
 */
std::shared_ptr<sw::redis::Redis> GatewayFactory::initRedis(
    const GatewayConfig::RedisConfig& _redisConfig)
{
    GATEWAY_FACTORY_LOG(INFO) << LOG_BADGE("initRedis") << LOG_DESC("start connect to redis")
                              << LOG_KV("host", _redisConfig.host)
                              << LOG_KV("port", _redisConfig.port) << LOG_KV("db", _redisConfig.db)
                              << LOG_KV("poolSize", _redisConfig.connectionPoolSize)
                              << LOG_KV("timeout", _redisConfig.timeout)
                              << LOG_KV("password", _redisConfig.password);

    sw::redis::ConnectionOptions connection_options;
    connection_options.host = _redisConfig.host;  // Required.
    connection_options.port = _redisConfig.port;  // Optional.
    connection_options.db = _redisConfig.db;      // Optional. Use the 0th database by default.
    if (!_redisConfig.password.empty())
    {
        connection_options.password = _redisConfig.password;  // Optional. No password by default.
    }

    // Optional. Timeout before we successfully send request to or receive response from redis.
    // By default, the timeout is 0ms, i.e. never timeout and block until we send or receive
    // successfully. NOTE: if any command is timed out, we throw a TimeoutError exception.
    connection_options.socket_timeout = std::chrono::milliseconds(_redisConfig.timeout);
    // connection_options.connect_timeout = std::chrono::milliseconds(3000);
    connection_options.keep_alive = true;

    sw::redis::ConnectionPoolOptions pool_options;
    // Pool size, i.e. max number of connections.
    pool_options.size = _redisConfig.connectionPoolSize;

    std::shared_ptr<sw::redis::Redis> redis = nullptr;
    try
    {
        // Connect to Redis server with a connection pool.
        redis = std::make_shared<sw::redis::Redis>(connection_options, pool_options);

        // test whether redis functions properly
        // 1. set key
        // 2. get key
        // 3. del key

        std::string key = "Gateway -> " + std::to_string(utcTime());
        std::string value = "Hello, FISCO-BCOS 3.0.";

        bool setR = redis->set(key, value);
        if (setR)
        {
            GATEWAY_FACTORY_LOG(INFO) << LOG_BADGE("initRedis") << LOG_DESC("set ok");

            auto getR = redis->get(key);
            if (getR)
            {
                GATEWAY_FACTORY_LOG(INFO) << LOG_BADGE("initRedis") << LOG_DESC("get ok")
                                          << LOG_KV("key", key) << LOG_KV("value", getR.value());
            }
            else
            {
                GATEWAY_FACTORY_LOG(WARNING)
                    << LOG_BADGE("initRedis") << LOG_DESC("get failed, why???");
            }

            redis->del(key);
        }
        else
        {
            GATEWAY_FACTORY_LOG(WARNING)
                << LOG_BADGE("initRedis") << LOG_DESC("set failed, why???");
        }
    }
    catch (std::exception& e)
    {
        // Note: redis++ exception handling
        //  https://github.com/sewenew/redis-plus-plus#exception
        std::exception_ptr ePtr = std::make_exception_ptr(e);

        GATEWAY_FACTORY_LOG(ERROR)
            << LOG_BADGE("initRedis") << LOG_DESC("initialize redis exception")
            << LOG_KV("error", e.what());

        std::throw_with_nested(e);
    }

    GATEWAY_FACTORY_LOG(INFO) << LOG_BADGE("initRedis") << LOG_DESC("initialize redis completely");

    return redis;
}

bcos::amop::AMOPImpl::Ptr GatewayFactory::buildAMOP(
    P2PInterface::Ptr _network, P2pID const& _p2pNodeID)
{
    auto topicManager = std::make_shared<TopicManager>(m_rpcServiceName, _network);
    auto amopMessageFactory = std::make_shared<AMOPMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AMOPImpl>(
        topicManager, amopMessageFactory, requestFactory, _network, _p2pNodeID);
}

bcos::amop::AMOPImpl::Ptr GatewayFactory::buildLocalAMOP(
    P2PInterface::Ptr _network, P2pID const& _p2pNodeID)
{
    // Note: must set rpc to the topicManager before start the amop
    auto topicManager = std::make_shared<LocalTopicManager>(m_rpcServiceName, _network);
    auto amopMessageFactory = std::make_shared<AMOPMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AMOPImpl>(
        topicManager, amopMessageFactory, requestFactory, _network, _p2pNodeID);
}
