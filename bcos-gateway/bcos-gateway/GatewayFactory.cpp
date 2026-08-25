/** @file GatewayFactory.cpp
 *  @author octopus
 *  @date 2021-05-17
 */

#include "bcos-gateway/GatewayFactory.h"
#include "bcos-boostssl/context/Common.h"
#include "bcos-crypto/signature/key/KeyFactoryImpl.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/GatewayConfig.h"
#include "bcos-gateway/gateway/GatewayMessageExtAttributes.h"
#include "bcos-gateway/gateway/GatewayNodeManager.h"
#include "bcos-gateway/gateway/ProGatewayNodeManager.h"
#include "bcos-gateway/libamop/AirTopicManager.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Host.h"
#include "bcos-gateway/libnetwork/PeerBlackWhitelistInterface.h"
#include "bcos-gateway/libnetwork/PeerBlacklist.h"
#include "bcos-gateway/libnetwork/PeerWhitelist.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/SessionCallback.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include "bcos-gateway/libratelimit/RateLimiterManager.h"
#include "bcos-tars-protocol/protocol/GroupInfoCodecImpl.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/FileUtility.h"
#include "bcos-utilities/IOServicePool.h"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <exception>
#include <optional>
#include <boost/exception_ptr.hpp>

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

        _pubHex = bcos::toHex(bytesConstRef((const byte*)pubKey->data, pubKey->length));

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

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC(
            "[NEW]initSSLContextPubHexHandlerWithoutExtInfo SSLContext pubHex: " + _pubHex);
        return true;
    };

    m_sslContextPubHandlerWithoutExtInfo = handler;
}

boost::asio::ssl::context GatewayFactory::buildSSLContext(
    bool _server, uint8_t sslMode, const GatewayConfig::CertConfig& _certConfig)
{
    std::ignore = _server;
    boost::asio::ssl::context sslContext(boost::asio::ssl::context::tlsv12);
    /*
      std::shared_ptr<EC_KEY> ecdh(EC_KEY_new_by_curve_name(NID_secp384r1),
                                   [](EC_KEY *p) { EC_KEY_free(p); });
      SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
   */
    if (_certConfig.nodeKey)
    {
        std::shared_ptr<bytes> keyContent;
        if (!_certConfig.nodeKey->empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    keyContent = readContents(boost::filesystem::path(*_certConfig.nodeKey));
                else
                    keyContent = m_dataEncrypt->decryptFile(*_certConfig.nodeKey);
            }
            catch (std::exception& e)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                    << LOG_KV("file", *_certConfig.nodeKey);
                BOOST_THROW_EXCEPTION(
                    InvalidParameter() << errinfo_comment(
                        "buildSSLContext: unable read content of key: " + *_certConfig.nodeKey));
            }
        }
        if (!keyContent || keyContent->empty())
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("buildSSLContext: unable read content of key: " + *_certConfig.nodeKey);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "buildSSLContext: unable read content of key: " + *_certConfig.nodeKey));
        }

        boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
        sslContext.use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);
    }
    // node.crt
    if (_certConfig.nodeCert)
    {
        sslContext.use_certificate_chain_file(*_certConfig.nodeCert);
    }
    /*if (!SSL_CTX_get0_certificate(sslContext->native_handle())) {
      GATEWAY_FACTORY_LOG(ERROR)
          << LOG_DESC("buildSSLContext: SSL_CTX_get0_certificate failed");
      BOOST_THROW_EXCEPTION(
          InvalidParameter() << errinfo_comment(
              "buildSSLContext: SSL_CTX_get0_certificate failed, node_cert=" +
              _certConfig.nodeCert));
    }*/

    if (_certConfig.caCert)
    {
        auto caCertContent =
            readContentsToString(boost::filesystem::path(*_certConfig.caCert));  // ca.crt
        if (!caCertContent || caCertContent->empty())
        {
            GATEWAY_FACTORY_LOG(ERROR)
                << LOG_DESC("buildSSLContext: unable read content of ca: " + *_certConfig.caCert);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "buildSSLContext: unable read content of ca: " + *_certConfig.caCert));
        }
        sslContext.add_certificate_authority(
            boost::asio::const_buffer(caCertContent->data(), caCertContent->size()));
    }
    std::string caPath = _certConfig.multiCaPath;
    if (!caPath.empty())
    {
        sslContext.add_verify_path(caPath);
    }

    sslContext.set_verify_mode(sslMode);

    return sslContext;
}

boost::asio::ssl::context GatewayFactory::buildSSLContext(
    bool _server, uint8_t sslMode, const GatewayConfig::SMCertConfig& _smCertConfig)
{
    SSL_CTX* ctx = nullptr;
    if (_server)
    {
        const SSL_METHOD* meth = SSLv23_server_method();
        ctx = SSL_CTX_new(meth);
        SSL_CTX_set_cipher_list(ctx,
            "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-"
            "SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECC-SM4-SM3:ECDHE-SM4-SM3");
    }
    else
    {
        const SSL_METHOD* meth = CNTLS_client_method();
        ctx = SSL_CTX_new(meth);
    }

    boost::asio::ssl::context sslContext(ctx);

    sslContext.set_verify_mode(boost::asio::ssl::context_base::verify_none);

    if (_smCertConfig.nodeCert)
    {
        /* Load the server certificate into the SSL_CTX structure */
        if (SSL_CTX_use_certificate_file(
                sslContext.native_handle(), _smCertConfig.nodeCert->c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stderr);
            BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_certificate_file failed"));
        }
    }

    if (_smCertConfig.nodeKey)
    {
        std::shared_ptr<bytes> keyContent;
        if (!_smCertConfig.nodeKey->empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    keyContent = readContents(boost::filesystem::path(*_smCertConfig.nodeKey));
                else
                    keyContent = m_dataEncrypt->decryptFile(*_smCertConfig.nodeKey);
            }
            catch (std::exception& e)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                    << LOG_KV("file", *_smCertConfig.nodeKey);
                BOOST_THROW_EXCEPTION(
                    InvalidParameter() << errinfo_comment(
                        "buildSSLContext: unable read content of key: " + *_smCertConfig.nodeKey));
            }
        }
        // nodekey
        boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
        sslContext.use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

        /* Check if the server certificate and private-key matches */
        if (!SSL_CTX_check_private_key(sslContext.native_handle()))
        {
            ERR_print_errors_fp(stderr);
            BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_private_key failed"));
        }
    }
    if (_smCertConfig.enNodeCert)
    {
        /* Load the server encrypt certificate into the SSL_CTX structure */
        if (SSL_CTX_use_enc_certificate_file(sslContext.native_handle(),
                _smCertConfig.enNodeCert->c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            ERR_print_errors_fp(stderr);
            BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_certificate_file failed"));
        }
    }
    if (_smCertConfig.enNodeKey)
    {
        std::shared_ptr<bytes> enNodeKeyContent;
        if (!_smCertConfig.enNodeKey->empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    enNodeKeyContent =
                        readContents(boost::filesystem::path(*_smCertConfig.enNodeKey));
                else
                    enNodeKeyContent = m_dataEncrypt->decryptFile(*_smCertConfig.enNodeKey);
            }
            catch (std::exception& e)
            {
                GATEWAY_FACTORY_LOG(ERROR)
                    << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                    << LOG_KV("file", *_smCertConfig.enNodeKey);
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "buildSSLContext: unable read content of key: " +
                                          *_smCertConfig.enNodeKey));
            }
        }
        std::string enNodeKeyStr((const char*)enNodeKeyContent->data(), enNodeKeyContent->size());
        if (SSL_CTX_use_enc_PrivateKey(
                sslContext.native_handle(), toEvpPkey(enNodeKeyStr.c_str())) <= 0)
        {
            GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("SSL_CTX_use_enc_PrivateKey failed");
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment("GatewayFactory::buildSSLContext "
                                                      "SSL_CTX_use_enc_PrivateKey failed"));
        }
    }

    if (_smCertConfig.caCert)
    {
        auto caContent = readContentsToString(
            boost::filesystem::path(*_smCertConfig.caCert));  // node.key content

        sslContext.add_certificate_authority(
            boost::asio::const_buffer(caContent->data(), caContent->size()));
    }
    std::string caPath = _smCertConfig.multiCaPath;
    if (!caPath.empty())
    {
        sslContext.add_verify_path(caPath);
    }

    sslContext.set_verify_mode(sslMode);

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
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
{
    auto rateLimiterStat =
        std::make_shared<ratelimiter::RateLimiterStat>(*m_ioServicePool->getIOService());
    rateLimiterStat->setStatInterval(_rateLimiterConfig.statInterval);
    rateLimiterStat->setEnableConnectDebugInfo(_rateLimiterConfig.enableConnectDebugInfo);

    auto rateLimiterManager = buildRateLimiterManager(_rateLimiterConfig);

    auto gatewayRateLimiter =
        std::make_shared<ratelimiter::GatewayRateLimiter>(rateLimiterManager, rateLimiterStat);

    return gatewayRateLimiter;
}

std::shared_ptr<gateway::ratelimiter::RateLimiterManager> GatewayFactory::buildRateLimiterManager(
    const GatewayConfig::RateLimiterConfig& _rateLimiterConfig)
{
    // rate limiter factory
    auto rateLimiterFactory = std::make_shared<ratelimiter::RateLimiterFactory>();
    // rate limiter manager
    auto rateLimiterManager = std::make_shared<ratelimiter::RateLimiterManager>(
        *m_ioServicePool->getIOService(), _rateLimiterConfig);

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
            auto rateLimiterInterface = rateLimiterFactory->buildTimeWindowRateLimiter(
                bandWidth * timeWindowS, toMillisecond(timeWindowS), allowExceedMaxPermitSize);

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
    auto nodeCert =
        (_config->smSSL() ? _config->smCertConfig().nodeCert : _config->certConfig().nodeCert);
    std::string pubHex;
    if (!nodeCert || !m_certPubHexHandler(*nodeCert, pubHex))
    {
        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  "GatewayFactory::init unable parse myself pub id"));
    }
    auto srvCtx = (_config->smSSL() ?
                       buildSSLContext(true, _config->sslServerMode(), _config->smCertConfig()) :
                       buildSSLContext(true, _config->sslServerMode(), _config->certConfig()));

    auto clientCtx =
        (_config->smSSL() ?
                buildSSLContext(false, _config->sslClientMode(), _config->smCertConfig()) :
                buildSSLContext(false, _config->sslClientMode(), _config->certConfig()));

    // IOServicePool must be set from outside before init()
    //
    // FIB-186: the pool that carries the acceptor, inbound + outbound sessions, TLS handshakes and
    // timers must actually be sized from configuration -- it used to be built as IOServicePool()
    // with no args, silently pinned to hardware_concurrency()+1 whatever the config said. That is
    // now handled upstream of this factory: p2p.thread_count is deprecated (see initP2PConfig) in
    // favour of the node-wide thread_pool.io_thread_count, which NodeConfig reads and the
    // initializer uses to size the shared pool injected here.
    //
    // There is deliberately NO second, acceptor-only pool to isolate inbound churn. A boost::asio
    // SSL stream is welded to its io_context for life, so a connection's handshake and the reads of
    // the session that follows run on the same pool; and an inbound churn connection is
    // indistinguishable from an inbound validator connection at accept time (identity is known only
    // after the handshake). So no pool assignment can separate attacker handshakes from validator
    // consensus reads -- both always land on the same pool. A pool split would only move which
    // thread the handshake CPU lands on, not off a thread that also carries consensus reads: thread
    // isolation is not CPU isolation, and once that thread saturates consensus halts anyway (a
    // split merely delays the onset). What actually prevents the halt is keeping the expensive
    // handshake from running at all -- rejecting churn cheaply BEFORE the handshake via the
    // accept-rate limit and the in-flight-handshake cap (Host), tuned small enough that admitted
    // handshake CPU cannot saturate the pool. This was validated on a 3-node churn harness: with
    // those caps on, a dedicated acceptor pool made no measurable difference; with them off, both
    // the single-pool and the split-pool builds halted. See
    // Host::DEFAULT_MAX_CONNECTIONS_PER_SECOND.
    if (!m_ioServicePool)
    {
        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  "GatewayFactory: IOServicePool must be provided from outside!"));
    }
    auto ioServicePool = m_ioServicePool;
    auto asioInterface =
        std::make_shared<ASIOInterface>(ioServicePool, _config->listenIP(), _config->listenPort());
    asioInterface->setSrvContext(std::move(srvCtx));
    asioInterface->setClientContext(std::move(clientCtx));
    asioInterface->setType(ASIOInterface::ASIO_TYPE::SSL);

    // Message Factory
    auto messageFactory = std::make_shared<P2PMessageFactoryV2>();
    auto nodeIDHash = _config->calculateShortNodeID(pubHex);
    P2PInfo selfInfo(nodeIDHash, pubHex);
    // Session Factory
    auto sessionFactory = std::make_shared<SessionFactory>(selfInfo,
        _config->sessionRecvBufferSize(), _config->allowMaxMsgSize(), _config->maxReadDataSize(),
        _config->maxSendDataSize(), _config->maxMsgCountSendOneTime(), _config->enableCompress());
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
    auto host =
        std::make_shared<Host>(_config->hashImpl(), asioInterface, sessionFactory, messageFactory);
    host->setHostPort(_config->listenIP(), _config->listenPort());
    host->setSSLContextPubHandler(m_sslContextPubHandler);
    host->setSSLContextPubHandlerWithoutExtInfo(m_sslContextPubHandlerWithoutExtInfo);
    host->setPeerBlacklist(peerBlacklist);
    host->setPeerWhitelist(peerWhitelist);
    host->setSessionCallbackManager(sessionCallbackManager);
    host->setEnableSslVerify(_config->enableSSLVerify());
    // FIB-184: apply the configured inbound-session caps (no longer hardcoded in Host)
    host->setMaxConcurrentSessions(_config->maxConcurrentSessions());
    host->setMaxSessionsPerIP(_config->maxSessionsPerIP());
    // FIB-186: bound in-flight TLS handshakes so connection churn cannot starve consensus reads.
    host->setMaxPendingHandshakes(_config->maxPendingHandshakes());
    host->setHandshakeTimeout(_config->handshakeTimeout());
    host->setMaxConnectionsPerSecond(_config->maxConnectionsPerSecond());
    // init Service
    bool enableRIPProtocol = _config->enableRIPProtocol();
    Service::Ptr service = nullptr;
    if (enableRIPProtocol)
    {
        auto routerTableFactory = std::make_shared<RouterTableFactoryImpl>();
        service = std::make_shared<ServiceV2>(
            selfInfo, routerTableFactory, *ioServicePool->getIOService());
    }
    else
    {
        service = std::make_shared<Service>(selfInfo);
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
                              << LOG_KV("myself pub id", printShortP2pID(pubHex))
                              << LOG_KV("myself_pub_id_hash", printShortP2pID(nodeIDHash));
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
            gatewayNodeManager = std::make_shared<GatewayNodeManager>(
                _config->uuid(), pubHex, keyFactory, service, *m_ioServicePool->getIOService());
            if (!_config->readonly())
            {
                amop = buildLocalAMOP(service, pubHex);
            }
        }
        else
        {
            // Note: no need to use nodeAliveDetector when enable failover
            if (_entryPoint)
            {
                gatewayNodeManager = std::make_shared<GatewayNodeManager>(
                    _config->uuid(), pubHex, keyFactory, service, *m_ioServicePool->getIOService());
            }
            else
            {
                gatewayNodeManager = std::make_shared<ProGatewayNodeManager>(
                    _config->uuid(), pubHex, keyFactory, service, *m_ioServicePool->getIOService());
            }

            if (!_config->readonly())
            {
                amop = buildAMOP(service, pubHex);
            }
            else
            {
                // register a null amop message handler
                service->registerHandlerByMsgType(GatewayMessageType::AMOPMessageType,
                    [](const bcos::gateway::NetworkException& _e,
                        const bcos::gateway::P2PSession::Ptr& session,
                        const std::shared_ptr<bcos::gateway::P2PMessage>& message) {
                        // 只读模式下, 不处理其它节点的amop消息
                        // In read-only mode, AMOP messages from other nodes are not processed
                        return;
                    });
            }
        }


        std::shared_ptr<ratelimiter::GatewayRateLimiter> gatewayRateLimiter;
        if (_config->rateLimiterConfig().enable)
        {
            gatewayRateLimiter = buildGatewayRateLimiter(_config->rateLimiterConfig());
        }

        // init Gateway
        auto gateway = std::make_shared<Gateway>(
            _config, service, gatewayNodeManager, amop, gatewayRateLimiter, _gatewayServiceName);
        if (_config->readonly())
        {
            gateway->enableReadOnlyMode();
        }
        auto gatewayNodeManagerWeakPtr = std::weak_ptr<GatewayNodeManager>(gatewayNodeManager);
        // register disconnect handler
        service->registerDisconnectHandler(
            [gatewayNodeManagerWeakPtr, serviceWeakPtr = std::weak_ptr<Service>(service)](
                NetworkException e, P2PSession::Ptr p2pSession) {
                if (e.errorCode() == P2PExceptionType::DuplicateSession ||
                    e.errorCode() == P2PExceptionType::Success)
                {
                    return;
                }
                auto gatewayNodeManager = gatewayNodeManagerWeakPtr.lock();
                if (!gatewayNodeManager || !p2pSession)
                {
                    return;
                }
                // FIB-186 (vector D): the teardown notification that drives this handler can be
                // delayed on the dedicated teardown executor while the SAME peer reconnects -- a
                // new session for the same p2pID is inserted and its status re-populates the
                // routing table. removeP2PID keys only on p2pID, so a stale teardown would erase
                // the new session's routing entries and blank the unicast route until the next
                // status broadcast. Only remove when this dropped session is still the session of
                // record for its p2pID (or none is): if a different, current session already owns
                // the p2pID the peer has reconnected and its entries must be kept.
                if (auto service = serviceWeakPtr.lock())
                {
                    auto current = service->getP2PSessionByNodeId(p2pSession->p2pID());
                    if (current && current != p2pSession)
                    {
                        return;
                    }
                }
                gatewayNodeManager->onRemoveNodeIDs(p2pSession->p2pID());
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
            service->setBeforeMessageHandler([gatewayRateLimiterWeakPtr](SessionFace& _session,
                                                 Message& _msg) -> std::optional<bcos::Error> {
                auto gatewayRateLimiter = gatewayRateLimiterWeakPtr.lock();
                if (!gatewayRateLimiter)
                {
                    return std::nullopt;
                }

                if (const auto* msgExtAttributes =
                        std::any_cast<const GatewayMessageExtAttributes*>(_msg.extAttributes()))
                {
                    std::string groupID =
                        msgExtAttributes ? msgExtAttributes->groupID() : std::string();
                    uint16_t moduleID = msgExtAttributes ? msgExtAttributes->moduleID() : 0;
                    std::string endpoint = _session.nodeIPEndpoint().address();
                    int64_t msgLength = _msg.length();
                    auto pkgType = _msg.packetType();

                    auto result = gatewayRateLimiter->checkOutGoing(
                        endpoint, pkgType, groupID, moduleID, msgLength);
                    return result ? std::make_optional(bcos::Error::buildError(
                                        "", OutBWOverflow, result.value())) :
                                    std::nullopt;
                }
                return {};
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
        boost::rethrow_exception(boost::current_exception());
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

bcos::amop::AMOPImpl::Ptr GatewayFactory::buildAMOP(
    P2PInterface::Ptr _network, P2pID const& _p2pNodeID)
{
    auto topicManager = std::make_shared<TopicManager>(m_rpcServiceName, _network);
    auto amopMessageFactory = std::make_shared<AMOPMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();

    auto service = std::dynamic_pointer_cast<Service>(_network);
    registerAMOPHandlers(service, topicManager);

    return std::make_shared<AMOPImpl>(topicManager, amopMessageFactory, requestFactory, _network,
        _p2pNodeID, *m_ioServicePool->getIOService(), m_ioServicePool);
}

bcos::amop::AMOPImpl::Ptr GatewayFactory::buildLocalAMOP(
    P2PInterface::Ptr _network, P2pID const& _p2pNodeID)
{
    // Note: must set rpc to the topicManager before start the amop
    auto topicManager = std::make_shared<LocalTopicManager>(m_rpcServiceName, _network);
    auto amopMessageFactory = std::make_shared<AMOPMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();

    auto service = std::dynamic_pointer_cast<Service>(_network);
    registerAMOPHandlers(service, topicManager);

    return std::make_shared<AMOPImpl>(topicManager, amopMessageFactory, requestFactory, _network,
        _p2pNodeID, *m_ioServicePool->getIOService(), m_ioServicePool);
}

void GatewayFactory::registerAMOPHandlers(
    std::shared_ptr<Service> const& service, TopicManager::Ptr const& topicManager)
{
    GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("registerAMOPHandlers");
    auto weakTopicManager = std::weak_ptr<TopicManager>(topicManager);
    // register disconnect handler
    service->registerDisconnectHandler(
        [weakTopicManager](NetworkException e, P2PSession::Ptr p2pSession) {
            if (e.errorCode() == P2PExceptionType::DuplicateSession ||
                e.errorCode() == P2PExceptionType::Success)
            {
                return;
            }
            auto topicMgr = weakTopicManager.lock();
            if (topicMgr && p2pSession)
            {
                topicMgr->onDisconnect(p2pSession->p2pID());
            }
        });

    service->registerUnreachableHandler([weakTopicManager](std::string const& _unreachableNode) {
        auto topicMgr = weakTopicManager.lock();
        if (!topicMgr)
        {
            return;
        }
        topicMgr->onDisconnect(_unreachableNode);
    });
}
