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
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::blockNumberI);
        this->bindAndAddMethod(jsonrpc::Procedure("pbftView", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::pbftViewI);
        this->bindAndAddMethod(jsonrpc::Procedure("consensusStatus", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_STRING, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::consensusStatusI);

        this->bindAndAddMethod(jsonrpc::Procedure("syncStatus", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::syncStatusI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("peers", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::peersI);

        this->bindAndAddMethod(jsonrpc::Procedure("getBlockByHash", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_BOOLEAN, NULL),
            &dev::rpc::RpcFace::getBlockByHashI);
        this->bindAndAddMethod(jsonrpc::Procedure("getBlockByNumber", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, "param3", jsonrpc::JSON_BOOLEAN, NULL),
            &dev::rpc::RpcFace::getBlockByNumberI);

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
            jsonrpc::Procedure("pendingTransactions", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, NULL),
            &dev::rpc::RpcFace::pendingTransactionsI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("call", jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                jsonrpc::JSON_INTEGER, "param2", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::RpcFace::callI);

        this->bindAndAddMethod(jsonrpc::Procedure("sendRawTransaction", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_INTEGER, "param2",
                                   jsonrpc::JSON_STRING, NULL),
            &dev::rpc::RpcFace::sendRawTransactionI);
    }

    inline virtual void blockNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->blockNumber(request[0u].asInt());
    }
    inline virtual void pbftViewI(const Json::Value& request, Json::Value& response)
    {
        response = this->pbftView(request[0u].asInt());
    }
    inline virtual void consensusStatusI(const Json::Value& request, Json::Value& response)
    {
        response = this->consensusStatus(request[0u].asInt());
    }

    inline virtual void syncStatusI(const Json::Value& request, Json::Value& response)
    {
        response = this->syncStatus(request[0u].asInt());
    }

    inline virtual void peersI(const Json::Value& request, Json::Value& response)
    {
        response = this->peers();
    }

    inline virtual void getBlockByHashI(const Json::Value& request, Json::Value& response)
    {
        response =
            this->getBlockByHash(request[0u].asInt(), request[1u].asString(), request[2u].asBool());
    }
    inline virtual void getBlockByNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->getBlockByNumber(
            request[0u].asInt(), request[1u].asString(), request[2u].asBool());
    }

    inline virtual void getTransactionByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByHash(request[0u].asInt(), request[1u].asString());
    }
    inline virtual void getTransactionByBlockHashAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockHashAndIndex(
            request[0u].asInt(), request[1u].asString(), request[2u].asString());
    }
    inline virtual void getTransactionByBlockNumberAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionByBlockNumberAndIndex(
            request[0u].asInt(), request[1u].asString(), request[2u].asString());
    }
    inline virtual void getTransactionReceiptI(const Json::Value& request, Json::Value& response)
    {
        response = this->getTransactionReceipt(request[0u].asInt(), request[1u].asString());
    }
    inline virtual void pendingTransactionsI(const Json::Value& request, Json::Value& response)
    {
        response = this->pendingTransactions(request[0u].asInt());
    }
    inline virtual void callI(const Json::Value& request, Json::Value& response)
    {
        response = this->call(request[0u].asInt(), request[1u]);
    }

    inline virtual void sendRawTransactionI(const Json::Value& request, Json::Value& response)
    {
        response = this->sendRawTransaction(request[0u].asInt(), request[1u].asString());
    }

    // consensus part
    virtual std::string blockNumber(int param1) = 0;
    virtual std::string pbftView(int param1) = 0;
    virtual std::string consensusStatus(int param1) = 0;

    // sync part
    virtual Json::Value syncStatus(int param1) = 0;

    // p2p part
    virtual Json::Value peers() = 0;

    // block part
    virtual Json::Value getBlockByHash(int param1, const std::string& param2, bool param3) = 0;
    virtual Json::Value getBlockByNumber(int param1, const std::string& param2, bool param3) = 0;

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
    /// @return information about pendingTransactions.
    virtual Json::Value pendingTransactions(int param1) = 0;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual std::string call(int param1, const Json::Value& param2) = 0;
    // Creates new message call transaction or a contract creation for signed transactions.
    virtual std::string sendRawTransaction(int param1, const std::string& param2) = 0;
};

}  // namespace rpc
}  // namespace dev
