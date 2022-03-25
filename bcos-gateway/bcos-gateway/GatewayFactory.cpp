/** @file GatewayFactory.cpp
 *  @author octopus
 *  @date 2021-05-17
 */

#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/gateway/ProGatewayNodeManager.h>
#include <bcos-gateway/libamop/AirTopicManager.h>
#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-gateway/libp2p/Service.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>

using namespace bcos::rpc;
using namespace bcos;
using namespace gateway;
using namespace bcos::amop;
using namespace bcos::protocol;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

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
            if (pubKey == NULL)
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
    const boostssl::context::ContextConfig::CertConfig& _certConfig)
{
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
    /*
      std::shared_ptr<EC_KEY> ecdh(EC_KEY_new_by_curve_name(NID_secp384r1),
                                   [](EC_KEY *p) { EC_KEY_free(p); });
      SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
   */
    auto keyContent =
        readContentsToString(boost::filesystem::path(_certConfig.nodeKey));  // node.key content
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
    const boostssl::context::ContextConfig::SMCertConfig& _smCertConfig)
{
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);

    auto keyContent =
        readContentsToString(boost::filesystem::path(_smCertConfig.nodeKey));  // node.key content

    boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    SSL_CTX_use_enc_certificate_file(
        sslContext->native_handle(), _smCertConfig.enNodeCert.c_str(), SSL_FILETYPE_PEM);
    if (SSL_CTX_use_enc_PrivateKey_file(
            sslContext->native_handle(), _smCertConfig.enNodeKey.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("SSL_CTX_use_enc_PrivateKey_file error");
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("GatewayFactory::buildSSLContext "
                                                  "SSL_CTX_use_enc_PrivateKey_file error"));
    }

    sslContext->use_certificate_chain_file(_smCertConfig.nodeCert);

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
std::shared_ptr<Gateway> GatewayFactory::buildGateway(
    const std::string& _configPath, bool _airVersion)
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
    return buildGateway(config, _airVersion);
}

/**
 * @brief: construct Gateway
 * @param _config: config parameter object
 * @return void
 */
std::shared_ptr<Gateway> GatewayFactory::buildGateway(GatewayConfig::Ptr _config, bool _airVersion)
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

        std::shared_ptr<ba::ssl::context> sslContext =
            (_config->smSSL() ? buildSSLContext(_config->smCertConfig()) :
                                buildSSLContext(_config->certConfig()));

        // // init ASIOInterface
        // auto asioInterface = std::make_shared<ASIOInterface>();
        // asioInterface->setIOService(std::make_shared<ba::io_service>());
        // asioInterface->setSSLContext(sslContext);
        // asioInterface->setType(ASIOInterface::ASIO_TYPE::SSL);

        // Message Factory
        auto messageFactory = std::make_shared<P2PMessageFactory>();
        // Session Factory
        auto sessionFactory = std::make_shared<SessionFactory>();
        // KeyFactory
        auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

        // init contextConfig
        auto contextConfig = std::make_shared<boostssl::context::ContextConfig>();
        std::string sslType = _config->smSSL() ? "sm_ssl" : "ssl";
        contextConfig->setSslType(sslType);
        if (_config->smSSL())
        {
            contextConfig->setSmCertConfig(_config->smCertConfig());
        }
        else
        {
            contextConfig->setCertConfig(_config->certConfig());
        }

        // init wsConfig
        auto wsConfig = std::make_shared<WsConfig>();

        // Mixed = server + client
        wsConfig->setModel(boostssl::ws::WsModel::Mixed);
        wsConfig->setListenIP(_config->listenIP());
        wsConfig->setListenPort(_config->listenPort());

        wsConfig->setThreadPoolSize(_config->threadPoolSize());
        wsConfig->setDisableSsl(false);
        wsConfig->setContextConfig(contextConfig);
        auto endPointPeers = _config->obtainPeersForWsService(_config->connectedNodes());
        wsConfig->setConnectedPeers(endPointPeers);

        // init wsservice
        auto wsService = std::make_shared<ws::WsService>();
        wsService->setHostPort(_config->listenIP(), _config->listenPort());
        auto wsInitializer = std::make_shared<WsInitializer>();
        wsInitializer->setConfig(wsConfig);
        wsInitializer->setMessageFactory(messageFactory);
        wsInitializer->initWsService(wsService);

        // init Service
        auto service = std::make_shared<Service>();
        service->setWsService(wsService);
        service->setStaticNodes(_config->connectedNodes());

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("GatewayFactory::init")
                                  << LOG_KV("myself pub id", pubHex)
                                  << LOG_KV("rpcService", m_rpcServiceName);

        service->setId(pubHex);
        service->setMessageFactory(messageFactory);
        service->setKeyFactory(keyFactory);

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
            gatewayNodeManager = std::make_shared<ProGatewayNodeManager>(
                _config->uuid(), pubHex, keyFactory, service);
            amop = buildAMOP(service, pubHex);
        }
        // init Gateway
        auto gateway = std::make_shared<Gateway>(m_chainID, service, gatewayNodeManager, amop);
        auto weakptrGatewayNodeManager = std::weak_ptr<GatewayNodeManager>(gatewayNodeManager);
        // register disconnect handler
        service->registerDisconnectHandler(
            [weakptrGatewayNodeManager](NetworkException e, P2PSession::Ptr p2pSession) {
                (void)e;
                auto gatewayNodeManager = weakptrGatewayNodeManager.lock();
                if (gatewayNodeManager && p2pSession)
                {
                    gatewayNodeManager->onRemoveNodeIDs(p2pSession->p2pID());
                }
            });

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("GatewayFactory::init ok");
        return gateway;
    }
    catch (const std::exception& e)
    {
        GATEWAY_FACTORY_LOG(ERROR) << LOG_DESC("GatewayFactory::init")
                                   << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
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