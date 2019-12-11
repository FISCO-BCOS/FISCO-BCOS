/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file RpcFace.cpp
 * @author: caryliao
 * @date 2018-10-26
 */
#pragma once

#include "ModularServer.h"
#include <boost/lexical_cast.hpp>
#include <set>

using namespace jsonrpc;

namespace dev
{
namespace rpc
{
class RpcFace : public ServerInterface<RpcFace>
{
public:
    RpcFace()
    {
        this->bindAndAddMethod(jsonrpc::Procedure("getSystemConfigByKey",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getSystemConfigByKeyI);
        this->bindAndAddMethod(jsonrpc::Procedure("getBlockNumber", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getBlockNumberI);
        this->bindAndAddMethod(jsonrpc::Procedure("getPbftView", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getPbftViewI);
        this->bindAndAddMethod(jsonrpc::Procedure("getSealerList", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getSealerListI);
        this->bindAndAddMethod(jsonrpc::Procedure("getObserverList", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getObserverListI);
        this->bindAndAddMethod(jsonrpc::Procedure("getConsensusStatus", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getConsensusStatusI);

        this->bindAndAddMethod(jsonrpc::Procedure("getSyncStatus", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getSyncStatusI);

        this->bindAndAddMethod(jsonrpc::Procedure("getClientVersion", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getClientVersionI);
        this->bindAndAddMethod(jsonrpc::Procedure("getPeers", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getPeersI);
        this->bindAndAddMethod(jsonrpc::Procedure("getGroupPeers", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getGroupPeersI);
        this->bindAndAddMethod(jsonrpc::Procedure("getGroupList", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getGroupListI);
        this->bindAndAddMethod(jsonrpc::Procedure("getNodeIDList", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getNodeIDListI);

        this->bindAndAddMethod(jsonrpc::Procedure("getBlockByHash", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_BOOLEAN, NULL),
            &dev::rpc::RpcFace::getBlockByHashI);
        this->bindAndAddMethod(jsonrpc::Procedure("getBlockByNumber", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_BOOLEAN, NULL),
            &dev::rpc::RpcFace::getBlockByNumberI);
        this->bindAndAddMethod(jsonrpc::Procedure("getBlockHashByNumber",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getBlockHashByNumberI);

        this->bindAndAddMethod(jsonrpc::Procedure("getTransactionByHash",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionByHashI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getTransactionByBlockHashAndIndex", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionByBlockHashAndIndexI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getTransactionByBlockNumberAndIndex", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionByBlockNumberAndIndexI);
        this->bindAndAddMethod(jsonrpc::Procedure("getTransactionReceipt",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionReceiptI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getPendingTransactions", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getPendingTransactionsI);
        this->bindAndAddMethod(jsonrpc::Procedure("getPendingTxSize", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getPendingTxSizeI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("call", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::callI);

        this->bindAndAddMethod(jsonrpc::Procedure("sendRawTransaction", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::sendRawTransactionI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("getCode", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT,
                "param1", jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getCodeI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getTotalTransactionCount", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::getTotalTransactionCountI);

        this->bindAndAddMethod(jsonrpc::Procedure("getTransactionByHashWithProof",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionByHashWithProofI);

        this->bindAndAddMethod(jsonrpc::Procedure("getTransactionReceiptByHashWithProof",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::getTransactionReceiptByHashWithProofI);

        this->bindAndAddMethod(jsonrpc::Procedure("generateGroup", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_INTEGER, "param3", jsonrpc::JSON_ARRAY, NULL),
            &dev::rpc::RpcFace::generateGroupI);

        this->bindAndAddMethod(jsonrpc::Procedure("startGroup", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::startGroupI);
    }

    inline virtual void getSystemConfigByKeyI(const Json::Value& request, Json::Value& response)
    {
        response = this->getSystemConfigByKey(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }
    inline virtual void getBlockNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockNumber(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getPbftViewI(const Json::Value& request, Json::Value& response)
    {
        response = this->getPbftView(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getSealerListI(const Json::Value& request, Json::Value& response)
    {
        response = this->getSealerList(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getObserverListI(const Json::Value& request, Json::Value& response)
    {
        response = this->getObserverList(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getConsensusStatusI(const Json::Value& request, Json::Value& response)
    {
        response = this->getConsensusStatus(boost::lexical_cast<int>(request[0u].asString()));
    }

    inline virtual void getSyncStatusI(const Json::Value& request, Json::Value& response)
    {
        response = this->getSyncStatus(boost::lexical_cast<int>(request[0u].asString()));
    }

    inline virtual void getClientVersionI(const Json::Value&, Json::Value& response)
    {
        response = this->getClientVersion();
    }
    inline virtual void getPeersI(const Json::Value& request, Json::Value& response)
    {
        response = this->getPeers(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getGroupPeersI(const Json::Value& request, Json::Value& response)
    {
        response = this->getGroupPeers(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getGroupListI(const Json::Value&, Json::Value& response)
    {
        response = this->getGroupList();
    }
    inline virtual void getNodeIDListI(const Json::Value& request, Json::Value& response)
    {
        response = this->getNodeIDList(boost::lexical_cast<int>(request[0u].asString()));
    }

    inline virtual void getBlockByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockByHash(boost::lexical_cast<int>(request[0u].asString()),
            request[1u].asString(), request[2u].asBool());
    }
    inline virtual void getBlockByNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockByNumber(boost::lexical_cast<int>(request[0u].asString()),
            request[1u].asString(), request[2u].asBool());
    }
    inline virtual void getBlockHashByNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockHashByNumber(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }

    inline virtual void getTransactionByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByHash(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }
    inline virtual void getTransactionByBlockHashAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockHashAndIndex(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString(),
            request[2u].asString());
    }
    inline virtual void getTransactionByBlockNumberAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockNumberAndIndex(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString(),
            request[2u].asString());
    }
    inline virtual void getTransactionReceiptI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionReceipt(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }
    inline virtual void getPendingTransactionsI(const Json::Value& request, Json::Value& response)
    {
        response = this->getPendingTransactions(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getPendingTxSizeI(const Json::Value& request, Json::Value& response)
    {
        response = this->getPendingTxSize(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void getCodeI(const Json::Value& request, Json::Value& response)
    {
        response =
            this->getCode(boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }
    inline virtual void getTotalTransactionCountI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTotalTransactionCount(boost::lexical_cast<int>(request[0u].asString()));
    }
    inline virtual void callI(const Json::Value& request, Json::Value& response)
    {
        response = this->call(boost::lexical_cast<int>(request[0u].asString()), request[1u]);
    }
    inline virtual void sendRawTransactionI(const Json::Value& request, Json::Value& response)
    {
        response = this->sendRawTransaction(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }

    inline virtual void getTransactionByHashWithProofI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByHashWithProof(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }


    inline virtual void getTransactionReceiptByHashWithProofI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionReceiptByHashWithProof(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString());
    }

    inline virtual void generateGroupI(const Json::Value& request, Json::Value& response)
    {
        std::set<std::string> sealerList;
        for (Json::Value value : request[2u])
        {
            std::string sealer = value.asString();
            sealerList.insert(sealer);
        }

        response = this->generateGroup(
            boost::lexical_cast<int>(request[0u].asString()), request[1u].asString(), sealerList);
    }

    inline virtual void startGroupI(const Json::Value& request, Json::Value& response)
    {
        response = this->startGroup(boost::lexical_cast<int>(request[0u].asString()));
    }

    // system config part
    virtual std::string getSystemConfigByKey(int param1, const std::string& param2) = 0;

    // consensus part
    virtual std::string getBlockNumber(int param1) = 0;
    virtual std::string getPbftView(int param1) = 0;
    virtual Json::Value getSealerList(int param1) = 0;
    virtual Json::Value getObserverList(int param1) = 0;
    virtual Json::Value getConsensusStatus(int param1) = 0;

    // sync part
    virtual Json::Value getSyncStatus(int param1) = 0;

    // p2p part
    virtual Json::Value getClientVersion() = 0;
    virtual Json::Value getPeers(int param1) = 0;
    virtual Json::Value getGroupPeers(int param1) = 0;
    virtual Json::Value getGroupList() = 0;
    virtual Json::Value getNodeIDList(int param1) = 0;

    // block part
    virtual Json::Value getBlockByHash(int param1, const std::string& param2, bool param3) = 0;
    virtual Json::Value getBlockByNumber(int param1, const std::string& param2, bool param3) = 0;
    virtual std::string getBlockHashByNumber(int param1, const std::string& param2) = 0;

    // transaction part
    /// @return the information about a transaction requested by transaction hash.
    virtual Json::Value getTransactionByHash(int param1, const std::string& param2) = 0;
    /// @return information about a transaction by block hash and transaction index position.
    virtual Json::Value getTransactionByBlockHashAndIndex(
        int param1, const std::string& param2, const std::string& param3) = 0;
    /// @return information about a transaction by block number and transaction index position.
    virtual Json::Value getTransactionByBlockNumberAndIndex(
        int param1, const std::string& param2, const std::string& param3) = 0;
    /// @return the receipt of a transaction by transaction hash.
    /// @note That the receipt is not available for pending transactions.
    virtual Json::Value getTransactionReceipt(int param1, const std::string& param2) = 0;
    /// @return information about PendingTransactions.
    virtual Json::Value getPendingTransactions(int param1) = 0;
    /// @return size about PendingTransactions.
    virtual std::string getPendingTxSize(int param1) = 0;
    /// Returns code at a given address.
    virtual std::string getCode(int param1, const std::string& param2) = 0;
    /// Returns the count of transactions and blocknumber.
    virtual Json::Value getTotalTransactionCount(int param1) = 0;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual Json::Value call(int param1, const Json::Value& param2) = 0;
    /// Creates new message call transaction or a contract creation for signed transactions.
    virtual std::string sendRawTransaction(int param1, const std::string& param2) = 0;
    // Get merkle transaction with proof by hash
    virtual Json::Value getTransactionByHashWithProof(int param1, const std::string& param2) = 0;
    // Get receipt with merkle proof by hash
    virtual Json::Value getTransactionReceiptByHashWithProof(
        int param1, const std::string& param2) = 0;

    // Group operation part
    virtual Json::Value generateGroup(
        int param1, const std::string& param2, const std::set<std::string>& param3) = 0;
    virtual Json::Value startGroup(int param1) = 0;
};

}  // namespace rpc
}  // namespace dev
