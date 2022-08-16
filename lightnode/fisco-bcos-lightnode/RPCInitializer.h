#pragma once

#include "client/LedgerClientImpl.h"
#include "client/P2PClientImpl.h"
#include "client/SchedulerClientImpl.h"
#include "client/TransactionPoolClientImpl.h"
#include <bcos-cpp-sdk/multigroup/JsonGroupInfoCodec.h>
#include <bcos-cpp-sdk/ws/HandshakeResponse.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-lightnode/rpc/LightNodeRPC.h>
#include <bcos-lightnode/storage/StorageImpl.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <json/forwards.h>
#include <json/value.h>
#include <libinitializer/ProtocolInitializer.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <thread>

namespace bcos::lightnode
{

static auto initRPC(bcos::tool::NodeConfig::Ptr nodeConfig, std::string nodeID,
    bcos::gateway::Gateway::Ptr gateway, bcos::crypto::KeyFactory::Ptr keyFactory,
    bcos::concepts::ledger::Ledger auto localLedger,
    bcos::concepts::ledger::Ledger auto remoteLedger,
    bcos::concepts::transacton_pool::TransactionPool auto transactionPool,
    bcos::concepts::scheduler::Scheduler auto scheduler)
{
    bcos::rpc::RpcFactory rpcFactory(nodeConfig->chainId(), gateway, keyFactory, nullptr);
    auto wsConfig = rpcFactory.initConfig(nodeConfig);
    auto wsService = rpcFactory.buildWsService(wsConfig);
    auto jsonrpc = std::make_shared<bcos::rpc::LightNodeRPC<decltype(localLedger),
        decltype(remoteLedger), decltype(transactionPool), decltype(scheduler),
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>>(localLedger, remoteLedger,
        transactionPool, scheduler, nodeConfig->chainId(), nodeConfig->groupId());

    wsService->registerMsgHandler(bcos::protocol::MessageType::HANDESHAKE,
        [nodeConfig, nodeID, localLedger](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) {
            RPC_LOG(INFO) << "LightNode handshake request";

            auto groupInfoCodec = std::make_shared<bcos::group::JsonGroupInfoCodec>();
            bcos::cppsdk::service::HandshakeResponse handshakeResponse(std::move(groupInfoCodec));

            auto status = bcos::concepts::getRef(localLedger).getStatus();

            handshakeResponse.mutableGroupBlockNumber().insert(
                std::make_pair(nodeConfig->groupId(), status.blockNumber));

            // Generate genesis info
            Json::Value genesisConfig;
            genesisConfig["consensusType"] = nodeConfig->consensusType();
            genesisConfig["blockTxCountLimit"] = nodeConfig->ledgerConfig()->blockTxCountLimit();
            genesisConfig["txGasLimit"] = (int64_t)(nodeConfig->txGasLimit());
            genesisConfig["consensusLeaderPeriod"] =
                nodeConfig->ledgerConfig()->leaderSwitchPeriod();
            Json::Value sealerList(Json::arrayValue);
            auto consensusNodeList = nodeConfig->ledgerConfig()->consensusNodeList();
            for (auto const& node : consensusNodeList)
            {
                Json::Value sealer;
                sealer["nodeID"] = node->nodeID()->hex();
                sealer["weight"] = node->weight();
                sealerList.append(sealer);
            }
            genesisConfig["sealerList"] = sealerList;
            Json::FastWriter fastWriter;
            std::string genesisConfigStr = fastWriter.write(genesisConfig);
            // Generate genesis info end

            auto groupInfo = std::make_shared<bcos::group::GroupInfo>();
            groupInfo->setChainID(nodeConfig->chainId());
            groupInfo->setGenesisConfig(genesisConfigStr);
            groupInfo->setGroupID(nodeConfig->groupId());
            groupInfo->setIniConfig("");

            auto nodeInfo = std::make_shared<bcos::group::ChainNodeInfo>();

            Json::Value iniConfig;
            iniConfig["isWasm"] = nodeConfig->isWasm();
            iniConfig["smCryptoType"] = nodeConfig->smCryptoType();
            iniConfig["chainID"] = nodeConfig->chainId();
            std::string iniStr = fastWriter.write(iniConfig);

            nodeInfo->setWasm(nodeConfig->isWasm());
            nodeInfo->setSmCryptoType(nodeConfig->smCryptoType());

            nodeInfo->setIniConfig(iniStr);
            nodeInfo->setMicroService(false);
            nodeInfo->setNodeName(nodeConfig->nodeName());
            nodeInfo->setNodeID(nodeID);
            nodeInfo->setNodeCryptoType((nodeConfig->smCryptoType() ? group::NodeCryptoType::SM_NODE : group::NodeCryptoType::NON_SM_NODE));

            auto protocol = bcos::protocol::ProtocolInfo();
            protocol.setMinVersion(4);
            protocol.setMaxVersion(1);
            nodeInfo->setNodeProtocol(std::move(protocol));
            nodeInfo->setNodeType(bcos::protocol::NodeType::None);
            groupInfo->appendNodeInfo(std::move(nodeInfo));

            std::vector<bcos::group::GroupInfo::Ptr> groupInfoList{std::move(groupInfo)};
            handshakeResponse.setGroupInfoList(groupInfoList);
            handshakeResponse.setProtocolVersion(1);

            std::string response;
            handshakeResponse.encode(response);

            msg->setPayload(std::make_shared<bcos::bytes>(response.begin(), response.end()));
            session->asyncSendMessage(msg);
            RPC_LOG(INFO) << LOG_DESC("LightNode handshake success")
                          << LOG_KV("version", session->version())
                          << LOG_KV("endpoint", session ? session->endPoint() : "unknown")
                          << LOG_KV("handshakeResponse", response);
        });
    wsService->registerMsgHandler(bcos::rpc::AMOPClientMessageType::AMOP_SUBTOPIC,
        [](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) {
            RPC_LOG(INFO) << "LightNode amop topic request";
        });
    wsService->registerMsgHandler(bcos::protocol::MessageType::RPC_REQUEST,
        [jsonrpc = std::move(jsonrpc)](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) mutable {
            auto buffer = msg->payload();
            auto req = std::string_view((const char*)buffer->data(), buffer->size());

            jsonrpc->onRPCRequest(req, [m_buffer = std::move(buffer), msg = std::move(msg),
                                           session = std::move(session)](bcos::bytes resp) {
                if (session && session->isConnected())
                {
                    auto buffer = std::make_shared<bcos::bytes>(std::move(resp));
                    msg->setPayload(buffer);
                    session->asyncSendMessage(msg);
                }
                else
                {
                    // remove the callback
                    RPC_LOG(WARNING)
                        << LOG_DESC("Unable to send response for session has been inactive")
                        << LOG_KV("req",
                               std::string_view((const char*)m_buffer->data(), m_buffer->size()))
                        << LOG_KV("resp", std::string_view((const char*)resp.data(), resp.size()))
                        << LOG_KV("seq", msg->seq())
                        << LOG_KV("endpoint", session ? session->endPoint() : std::string(""));
                }
            });
        });
    return wsService;
}

}  // namespace bcos::lightnode