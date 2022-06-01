/** @file GatewayFactory.cpp
 *  @author octopus
 *  @date 2021-05-17
 */

#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/context/NodeInfoTools.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/gateway/ProGatewayNodeManager.h>
#include <bcos-gateway/libamop/AirTopicManager.h>
#include <bcos-gateway/libp2p/P2PMessageV2.h>
#include <bcos-gateway/libp2p/ServiceV2.h>
#include <bcos-gateway/libp2p/router/RouterTableImpl.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>

using namespace bcos::rpc;
using namespace bcos;
using namespace security;
using namespace gateway;
using namespace bcos::amop;
using namespace bcos::protocol;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::context;

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
        std::string nodeCert = (_config->wsConfig()->smSSL() ? _config->smCertConfig().nodeCert :
                                                               _config->certConfig().nodeCert);
        std::string pubHex;
        auto certPubHexHandler = NodeInfoTools::initCert2PubHexHandler();
        if (!certPubHexHandler(nodeCert, pubHex))
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "GatewayFactory::init unable parse myself pub id"));
        }

        // Message Factory
        auto messageFactory = std::make_shared<P2PMessageFactoryV2>();
        // Session Factory
        auto sessionFactory = std::make_shared<P2pSessionFactory>(pubHex);
        // KeyFactory
        auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();

        // init wsservice
        auto wsService = std::make_shared<ws::WsService>(_config->wsConfig()->moduleName());
        auto wsInitializer = std::make_shared<WsInitializer>();
        wsInitializer->setConfig(_config->wsConfig());
        wsInitializer->setMessageFactory(messageFactory);
        wsInitializer->setSessionFactory(sessionFactory);
        wsInitializer->initWsService(wsService);

        // init Service
        auto routerTableFactory = std::make_shared<RouterTableFactoryImpl>();
        auto service = std::make_shared<ServiceV2>(pubHex, wsService, routerTableFactory);
        service->setStaticNodes(*(_config->wsConfig()->connectPeers()).get());

        GATEWAY_FACTORY_LOG(INFO) << LOG_DESC("GatewayFactory::init")
                                  << LOG_KV("myself pub id", pubHex)
                                  << LOG_KV("rpcService", m_rpcServiceName)
                                  << LOG_KV("gatewayServiceName", _gatewayServiceName);
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
        auto gateway = std::make_shared<Gateway>(
            m_chainID, service, gatewayNodeManager, amop, _gatewayServiceName);
        auto weakptrGatewayNodeManager = std::weak_ptr<GatewayNodeManager>(gatewayNodeManager);
        // register disconnect handler
        service->registerDisconnectHandler([weakptrGatewayNodeManager](P2PSession::Ptr p2pSession) {
            auto gatewayNodeManager = weakptrGatewayNodeManager.lock();
            if (gatewayNodeManager && p2pSession)
            {
                gatewayNodeManager->onRemoveNodeIDs(p2pSession->p2pID());
            }
        });
        service->registerUnreachableHandler(
            [weakptrGatewayNodeManager](std::string const& _unreachableNode) {
                auto nodeMgr = weakptrGatewayNodeManager.lock();
                if (!nodeMgr)
                {
                    return;
                }
                nodeMgr->onRemoveNodeIDs(_unreachableNode);
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