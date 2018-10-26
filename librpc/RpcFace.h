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
 * @brief: unit test for Session
 *
 * @file RpcFace.cpp
 * @author: caryliao
 * @date 2018-10-26
 */
#pragma once

#include "ModularServer.h"

namespace dev
{
namespace rpc
{
class RpcFace : public ServerInterface<RpcFace>
{
public:
	RpcFace()
    {
        this->bindAndAddMethod(jsonrpc::Procedure("blockNumber", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::blockNumberI);
        this->bindAndAddMethod(jsonrpc::Procedure("pbftView", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::pbftViewI);

        this->bindAndAddMethod(jsonrpc::Procedure("peers", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::peersI);

        this->bindAndAddMethod(jsonrpc::Procedure("getBlockByHash", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getBlockByHashI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getBlockByNumber", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getBlockByNumberI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("getTransactionByHash", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getTransactionByHashI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getTransactionByBlockHashAndIndex", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getTransactionByBlockHashAndIndexI);
        this->bindAndAddMethod(jsonrpc::Procedure("getTransactionByBlockNumberAndIndex",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getTransactionByBlockNumberAndIndexI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("getTransactionReceipt", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::getTransactionReceiptI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("pendingTransactions", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::pendingTransactionsI);
        this->bindAndAddMethod(jsonrpc::Procedure("call", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::callI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("sendRawTransaction", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::sendRawTransactionI);

        this->bindAndAddMethod(jsonrpc::Procedure("topics", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::topicsI);
        this->bindAndAddMethod(jsonrpc::Procedure("setTopics", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::setTopicsI);
        this->bindAndAddMethod(jsonrpc::Procedure("sendMessage", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::sendMessageI);
    }

    inline virtual void blockNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->blockNumber(request);
    }
    inline virtual void pbftViewI(const Json::Value& request, Json::Value& response)
    {
        response = this->pbftView(request);
    }

    inline virtual void peersI(const Json::Value& request, Json::Value& response)
    {
        response = this->peers(request);
    }

    inline virtual void getBlockByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockByHash(request);
    }
    inline virtual void getBlockByNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockByNumber(request);
    }

    inline virtual void getTransactionByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByHash(request);
    }
    inline virtual void getTransactionByBlockHashAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockHashAndIndex(request);
    }
    inline virtual void getTransactionByBlockNumberAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockNumberAndIndex(request);
    }
    inline virtual void getTransactionReceiptI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionReceipt(request);
    }
    inline virtual void pendingTransactionsI(const Json::Value& request, Json::Value& response)
    {
        response = this->pendingTransactions(request);
    }
    inline virtual void callI(const Json::Value& request, Json::Value& response)
    {
        response = this->call(request);
    }

    inline virtual void sendRawTransactionI(const Json::Value& request, Json::Value& response)
    {
        response = this->sendRawTransaction(request);
    }

    inline virtual void topicsI(const Json::Value& request, Json::Value& response)
    {
        response = this->topics(request);
    }
    inline virtual void setTopicsI(const Json::Value& request, Json::Value& response)
    {
        response = this->setTopics(request);
    }
    inline virtual void sendMessageI(const Json::Value& request, Json::Value& response)
    {
        response = this->sendMessage(request);
    }

    // consensus part
    virtual Json::Value blockNumber(const Json::Value& requestJson) = 0;
    virtual Json::Value pbftView(const Json::Value& requestJson) = 0;

    // p2p part
    virtual Json::Value peers(const Json::Value& requestJson) = 0;

    // block part
    virtual Json::Value getBlockByHash(const Json::Value& requestJson) = 0;
    virtual Json::Value getBlockByNumber(const Json::Value& requestJson) = 0;

    // transaction part
    /// @return the information about a transaction requested by transaction hash.
    virtual Json::Value getTransactionByHash(const Json::Value& requestJson) = 0;
    /// @return information about a transaction by block hash and transaction index position.
    virtual Json::Value getTransactionByBlockHashAndIndex(const Json::Value& requestJson) = 0;
    /// @return information about a transaction by block number and transaction index position.
    virtual Json::Value getTransactionByBlockNumberAndIndex(const Json::Value& requestJson) = 0;
    /// @return the receipt of a transaction by transaction hash.
    /// @note That the receipt is not available for pending transactions.
    virtual Json::Value getTransactionReceipt(const Json::Value& requestJson) = 0;
    /// @return information about pendingTransactions.
    virtual Json::Value pendingTransactions(const Json::Value& requestJson) = 0;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual Json::Value call(const Json::Value& requestJson) = 0;

    // Creates new message call transaction or a contract creation for signed transactions.
    virtual Json::Value sendRawTransaction(const Json::Value& requestJson) = 0;

    // amop part
    virtual Json::Value topics(const Json::Value& requestJson) = 0;
    virtual Json::Value setTopics(const Json::Value& requestJson) = 0;
    virtual Json::Value sendMessage(const Json::Value& requestJson) = 0;
};

}  // namespace rpc
}  // namespace dev
