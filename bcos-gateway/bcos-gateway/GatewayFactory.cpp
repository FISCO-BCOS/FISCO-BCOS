/** @file GatewayFactory.cpp
 *  @author octopus
 *  @date 2021-05-17
 */

#include "bcos-gateway/GatewayConfig.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/ratelimiter/DistributedRateLimiter.h"
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
#include <bcos-gateway/libnetwork/PeerBlackWhitelistInterface.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libp2p/P2PMessageV2.h>
#include <bcos-gateway/libp2p/ServiceV2.h>
#include <bcos-gateway/libp2p/router/RouterTableImpl.h>
#include <bcos-gateway/libratelimit/RateLimiterManager.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/ratelimiter/TokenBucketRateLimiter.h>
#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <chrono>
#include <exception>
#include <optional>
#include <thread>

using namespace bcos::rpc;
using namespace bcos;
using namespace security;
using namespace gateway;
using namespace bcos::amop;
using namespace bcos::protocol;
using namespace bcos::boostssl;

struct GatewayP2PReloadHandler
{
    static GatewayConfig::Ptr config;
    static Service::Ptr service;

    static void handle(int sig)
    {
        std::unique_lock<std::mutex> lock(g_BCOSConfig.signalMutex());
        BCOS_LOG(INFO) << LOG_BADGE("Gateway::Signal") << LOG_DESC("receive SIGUSER1 sig");

        if (!config || !service)
        {
            return;
        }

        try
        {
            config->loadP2pConnectedNodes();
            auto nodes = config->connectedNodes();
            service->setStaticNodes(nodes);

            config->loadPeerBlacklist();
            service->updatePeerBlacklist(config->peerBlacklist(), config->enableBlacklist());

            config->loadPeerWhitelist();
            service->updatePeerWhitelist(config->peerWhitelist(), config->enableWhitelist());

            BCOS_LOG(INFO) << LOG_BADGE("Gateway::Signal")
                           << LOG_DESC("reload p2p connected nodes successfully")
                           << LOG_KV("nodes count: ", nodes.size());
        }
        catch (const std::exception& e)
        {
            BCOS_LOG(WARNING) << LOG_BADGE("Gateway::Signal")
                              << LOG_DESC("reload p2p connected nodes failed, e: " +
                                          std::string(e.what()));
        }
    }
};

GatewayConfig::Ptr GatewayP2PReloadHandler::config = nullptr;
Service::Ptr GatewayP2PReloadHandler::service = nullptr;

// register the function fetch pub hex from the cert
void GatewayFactory::initCert2PubHexHandler()
{
    auto handler = [this](const std::string& _cert, std::string& _pubHex) -> bool {
        auto certContent = readContentsToString(boost::filesystem::path(_cert));
        if (!certContent || certContent->empty())
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                << LOG_KV("message", "unable to load cert content, cert: " + _cert);
            return false;
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
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                << LOG_KV("message", "BIO_new error");
            return false;
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
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                << LOG_KV("message", "PEM_read_bio_X509 error");
            return false;
        }

        return m_sslContextPubHandler(x509Ptr.get(), _pubHex);
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
                << LOG_DESC("initSSLContextPubHexHandler X509_get0_pubkey_bitstr failed");
            return false;
        }

        auto hex = bcos::toHexString(pubKey->data, pubKey->data + pubKey->length, "");
        _pubHex = *hex.get();

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("[NEW]SSLContext pubHex: " + _pubHex);
        return true;
    };

    m_sslContextPubHandler = handler;
}

// register the function fetch public key from the ssl context
void GatewayFactory::initSSLContextPubHexHandlerWithoutExtInfo()
{
    auto handler = [](X509* x509, std::string& _pubHex) -> bool {
        EVP_PKEY* pKey = X509_get_pubkey(x509);
        if (nullptr == pKey)
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initSSLContextPubHexHandler X509_get_pubkey failed");
            return false;
        }

        int type = EVP_PKEY_base_id(pKey);
        if (EVP_PKEY_RSA == type || EVP_PKEY_RSA2 == type)
        {
            RSA* rsa = EVP_PKEY_get0_RSA(pKey);
            if (nullptr == rsa)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_DESC("initSSLContextPubHexHandler EVP_PKEY_get0_RSA failed");
                return false;
            }

            const BIGNUM* n = RSA_get0_n(rsa);
            if (nullptr == n)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_DESC("initSSLContextPubHexHandler RSA_get0_n failed");
                return false;
            }

            _pubHex = BN_bn2hex(n);  // RSA_print_fp(stdout, rsa, 0);
        }
        else if (EVP_PKEY_EC == type)
        {
            ec_key_st* ecPublicKey = EVP_PKEY_get0_EC_KEY(pKey);
            if (nullptr == ecPublicKey)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_DESC("initSSLContextPubHexHandler EVP_PKEY_get1_EC_KEY failed");
                return false;
            }

            const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
            if (nullptr == ecPoint)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_DESC("initSSLContextPubHexHandler EC_KEY_get0_public_key failed");
                return false;
            }

            const EC_GROUP* ecGroup = EC_KEY_get0_group(ecPublicKey);
            if (nullptr == ecGroup)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_DESC("initSSLContextPubHexHandler EC_KEY_get0_group failed");
                return false;
            }

            std::shared_ptr<char> hex = std::shared_ptr<char>(
                EC_POINT_point2hex(ecGroup, ecPoint, EC_KEY_get_conv_form(ecPublicKey), NULL),
                [](char* p) { OPENSSL_free(p); });
            if (nullptr != hex)
            {
                if ('0' == *(hex.get()) && '4' == *(hex.get() + 1))
                    _pubHex = hex.get() + 2;
                else
                    _pubHex = hex.get();
            }
        }
        else
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("initSSLContextPubHexHandler unknown type failed")
                << LOG_KV("type", type);

            return false;
        }

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("[NEW]SSLContext pubHex: " + _pubHex);
        return true;
    };

    m_sslContextPubHandlerWithoutExtInfo = handler;
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

    std::string caPath = _certConfig.multiCaPath;
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
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_certificate_file failed"));
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
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_private_key failed"));
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
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_certificate_file failed"));
    }
    std::string enNodeKeyStr((const char*)enNodeKeyContent->data(), enNodeKeyContent->size());

    if (SSL_CTX_use_enc_PrivateKey(sslContext->native_handle(), toEvpPkey(enNodeKeyStr.c_str())) <=
        0)
    {
        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("SSL_CTX_use_enc_PrivateKey failed");
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("GatewayFactory::buildSSLContext "
                                                  "SSL_CTX_use_enc_PrivateKey failed"));
    }
    auto caContent =
        readContentsToString(boost::filesystem::path(_smCertConfig.caCert));  // node.key content

    sslContext->add_certificate_authority(
        boost::asio::const_buffer(caContent->data(), caContent->size()));

    std::string caPath = _smCertConfig.multiCaPath;
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
    config->setConfigFile(_configPath);
    return buildGateway(config, _airVersion, _entryPoint, _gatewayServiceName);
}

std::shared_ptr<gateway::ratelimiter::GatewayRateLimiter> GatewayFactory::buildGatewayRateLimiter(
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
    const GatewayConfig::RedisConfig& _redisConfig)
{
    auto rateLimiterStat = std::make_shared<ratelimiter::RateLimiterStat>();
    rateLimiterStat->setStatInterval(_rateLimiterConfig.statInterval);
    rateLimiterStat->setEnableConnectDebugInfo(_rateLimiterConfig.enableConnectDebugInfo);

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

std::shared_ptr<gateway::ratelimiter::RateLimiterManager> GatewayFactory::buildRateLimiterManager(
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig,
    std::shared_ptr<sw::redis::Redis> _redis)
{
    // rate limiter factory
    auto rateLimiterFactory = std::make_shared<ratelimiter::RateLimiterFactory>(_redis);
    // rate limiter manager
    auto rateLimiterManager = std::make_shared<ratelimiter::RateLimiterManager>(_rateLimiterConfig);

    int32_t timeWindowS = _rateLimiterConfig.timeWindowSec;
    bool allowExceedMaxPermitSize = _rateLimiterConfig.allowExceedMaxPermitSize;

    // total outgoing bandwidth Limit for p2p network
    bcos::ratelimiter::RateLimiterInterface::Ptr totalOutgoingRateLimiter = nullptr;
    if (_rateLimiterConfig.totalOutgoingBwLimit > 0)
    {
        totalOutgoingRateLimiter = rateLimiterFactory->buildTimeWindowRateLimiter(
            _rateLimiterConfig.totalOutgoingBwLimit * timeWindowS, toMillisecond(timeWindowS),
            allowExceedMaxPermitSize);

        rateLimiterManager->registerRateLimiter(
            ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY, totalOutgoingRateLimiter);
    }

    // ip connection => rate limit
    if (!_rateLimiterConfig.ip2BwLimit.empty())
    {
        for (const auto& [ip, bandWidth] : _rateLimiterConfig.ip2BwLimit)
        {
            auto rateLimiterInterface = rateLimiterFactory->buildTimeWindowRateLimiter(
                bandWidth * timeWindowS, toMillisecond(timeWindowS), allowExceedMaxPermitSize);
            rateLimiterManager->registerRateLimiter(ip, rateLimiterInterface);
        }
    }

    // group => rate limit
    if (!_rateLimiterConfig.group2BwLimit.empty())
    {
        for (const auto& [group, bandWidth] : _rateLimiterConfig.group2BwLimit)
        {
            bcos::ratelimiter::RateLimiterInterface::Ptr rateLimiterInterface = nullptr;
            if (_rateLimiterConfig.enableDistributedRatelimit)
            {
                rateLimiterInterface = rateLimiterFactory->buildDistributedRateLimiter(
                    rateLimiterFactory->toTokenKey(group), bandWidth * timeWindowS, timeWindowS,
                    allowExceedMaxPermitSize, _rateLimiterConfig.enableDistributedRateLimitCache,
                    _rateLimiterConfig.distributedRateLimitCachePercent);
            }
            else
            {
                rateLimiterInterface = rateLimiterFactory->buildTimeWindowRateLimiter(
                    bandWidth * timeWindowS, toMillisecond(timeWindowS), allowExceedMaxPermitSize);
            }

            rateLimiterManager->registerRateLimiter(group, rateLimiterInterface);
        }
    }

    // modules without bandwidth limit
    rateLimiterManager->resetModulesWithoutLimit(_rateLimiterConfig.modulesWithoutLimit);
    rateLimiterManager->setRateLimiterFactory(rateLimiterFactory);
    rateLimiterManager->setEnableInRateLimit(_rateLimiterConfig.enableInRateLimit());
    rateLimiterManager->setEnableOutConRateLimit(_rateLimiterConfig.enableOutConnRateLimit());
    rateLimiterManager->setEnableOutGroupRateLimit(_rateLimiterConfig.enableOutGroupRateLimit());
    if (!_rateLimiterConfig.p2pBasicMsgTypes.empty())
    {
        rateLimiterManager->resetP2pBasicMsgTypes(_rateLimiterConfig.p2pBasicMsgTypes);
    }

    return rateLimiterManager;
}

//
std::shared_ptr<Service> GatewayFactory::buildService(const GatewayConfig::Ptr& _config)
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
    auto ioServicePool = std::make_shared<IOServicePool>();
    asioInterface->setIOServicePool(ioServicePool);
    asioInterface->setSrvContext(srvCtx);
    asioInterface->setClientContext(clientCtx);
    asioInterface->setType(ASIOInterface::ASIO_TYPE::SSL);

    // Message Factory
    auto messageFactory = std::make_shared<P2PMessageFactoryV2>();
    // Session Factory
    auto sessionFactory = std::make_shared<SessionFactory>(pubHex, _config->sessionRecvBufferSize(),
        _config->allowMaxMsgSize(), _config->maxReadDataSize(), _config->maxSendDataSize(),
        _config->maxMsgCountSendOneTime(), _config->enableCompress());
    // KeyFactory
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    // Session Callback manager
    auto sessionCallbackManager = std::make_shared<SessionCallbackManagerBucket>();

    // init peer black list
    PeerBlackWhitelistInterface::Ptr peerBlacklist =
        std::make_shared<PeerBlacklist>(_config->peerBlacklist(), _config->enableBlacklist());
    // init peer white list
    PeerBlackWhitelistInterface::Ptr peerWhitelist =
        std::make_shared<PeerWhitelist>(_config->peerWhitelist(), _config->enableWhitelist());

    // init Host
    auto host = std::make_shared<Host>(asioInterface, sessionFactory, messageFactory);
    host->setHostPort(_config->listenIP(), _config->listenPort());
    host->setSSLContextPubHandler(m_sslContextPubHandler);
    host->setSSLContextPubHandlerWithoutExtInfo(m_sslContextPubHandlerWithoutExtInfo);
    host->setPeerBlacklist(peerBlacklist);
    host->setPeerWhitelist(peerWhitelist);
    host->setSessionCallbackManager(sessionCallbackManager);

    // init Service
    bool enableRIPProtocol = _config->enableRIPProtocol();
    Service::Ptr service = nullptr;
    if (enableRIPProtocol)
    {
        auto routerTableFactory = std::make_shared<RouterTableFactoryImpl>();
        service = std::make_shared<ServiceV2>(pubHex, routerTableFactory);
    }
    else
    {
        service = std::make_shared<Service>(pubHex);
    }

    service->setHost(host);
    service->setStaticNodes(_config->connectedNodes());

    GatewayP2PReloadHandler::config = _config;
    GatewayP2PReloadHandler::service = service;
    // register SIGUSR1 for reload connected p2p nodes config
    signal(GATEWAY_RELOAD_P2P_CONFIG, GatewayP2PReloadHandler::handle);

    BCOS_LOG(INFO) << LOG_DESC("register SIGUSR1 sig for reload p2p connected nodes config");

    GATEWAY_FACTORY_LOG(INFO) << LOG_BADGE("buildService") << LOG_DESC("build service end")
                              << LOG_KV("enable rip protocol", _config->enableRIPProtocol())
                              << LOG_KV("enable compress", _config->enableCompress())
                              << LOG_KV("myself pub id", pubHex);
    service->setMessageFactory(messageFactory);
    service->setKeyFactory(keyFactory);
    return service;
}

/**
 * @brief: construct Gateway
 * @param _config: config parameter object
 * @return void
 */
// Note: _gatewayServiceName is used to check the validation of groupInfo when localRouter
// update groupInfo
std::shared_ptr<Gateway> GatewayFactory::buildGateway(GatewayConfig::Ptr _config, bool _airVersion,
    bcos::election::LeaderEntryPointInterface::Ptr _entryPoint,
    std::string const& _gatewayServiceName)
{
    try
    {
        auto service = buildService(_config);
        auto pubHex = service->id();
        auto keyFactory = service->keyFactory();

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

        std::shared_ptr<ratelimiter::GatewayRateLimiter> gatewayRateLimiter;
        if (_config->rateLimiterConfig().enable)
        {
            gatewayRateLimiter =
                buildGatewayRateLimiter(_config->rateLimiterConfig(), _config->redisConfig());
        }

        // init Gateway
        auto gateway = std::make_shared<Gateway>(
            _config, service, gatewayNodeManager, amop, gatewayRateLimiter, _gatewayServiceName);
        auto gatewayNodeManagerWeakPtr = std::weak_ptr<GatewayNodeManager>(gatewayNodeManager);
        // register disconnect handler
        service->registerDisconnectHandler(
            [gatewayNodeManagerWeakPtr](NetworkException e, P2PSession::Ptr p2pSession) {
                (void)e;
                auto gatewayNodeManager = gatewayNodeManagerWeakPtr.lock();
                if (gatewayNodeManager && p2pSession)
                {
                    gatewayNodeManager->onRemoveNodeIDs(p2pSession->p2pID());
                }
            });

        service->registerUnreachableHandler(
            [gatewayNodeManagerWeakPtr](std::string const& _unreachableNode) {
                auto nodeMgr = gatewayNodeManagerWeakPtr.lock();
                if (!nodeMgr)
                {
                    return;
                }
                nodeMgr->onRemoveNodeIDs(_unreachableNode);
            });

        if (gatewayRateLimiter)
        {
            auto gatewayRateLimiterWeakPtr =
                std::weak_ptr<ratelimiter::GatewayRateLimiter>(gatewayRateLimiter);
            service->setBeforeMessageHandler([gatewayRateLimiterWeakPtr](SessionFace::Ptr _session,
                                                 Message::Ptr _msg) -> std::optional<bcos::Error> {
                auto gatewayRateLimiter = gatewayRateLimiterWeakPtr.lock();
                if (!gatewayRateLimiter)
                {
                    return std::nullopt;
                }

                GatewayMessageExtAttributes::Ptr msgExtAttributes = nullptr;
                if (_msg->extAttributes())
                {
                    msgExtAttributes = std::dynamic_pointer_cast<GatewayMessageExtAttributes>(
                        _msg->extAttributes());
                }

                std::string groupID =
                    msgExtAttributes ? msgExtAttributes->groupID() : std::string();
                uint16_t moduleID = msgExtAttributes ? msgExtAttributes->moduleID() : 0;
                std::string endpoint = _session->nodeIPEndpoint().address();
                int64_t msgLength = _msg->length();
                auto pkgType = _msg->packetType();

                auto result = gatewayRateLimiter->checkOutGoing(
                    endpoint, pkgType, groupID, moduleID, msgLength);
                return result ? std::make_optional(
                                    bcos::Error::buildError("", OutBWOverflow, result.value())) :
                                std::nullopt;
            });

            service->setOnMessageHandler([gatewayRateLimiterWeakPtr](SessionFace::Ptr _session,
                                             Message::Ptr _message) -> std::optional<bcos::Error> {
                auto gatewayRateLimiter = gatewayRateLimiterWeakPtr.lock();
                if (!gatewayRateLimiter)
                {
                    return std::nullopt;
                }

                auto endpoint = _session->nodeIPEndpoint().address();
                auto packetType = _message->packetType();
                auto msgLength = _message->length();

                auto result =
                    gatewayRateLimiter->checkInComing(endpoint, packetType, msgLength, true);
                return result ? std::make_optional(
                                    bcos::Error::buildError("", InQPSOverflow, result.value())) :
                                std::nullopt;
                return std::nullopt;
            });
        }

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
                                   << LOG_KV("message", boost::diagnostic_information(e));
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
                    GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("memberChangedNotification failed")
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
            << LOG_KV("message", e.what());

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
