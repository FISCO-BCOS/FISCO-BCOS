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
 * @file Eth.cpp
 * @author: caryliao
 * @date 2018-10-26
 */
#ifndef JSONRPC_CPP_STUB_DEV_RPC_ETHFACE_H_
#define JSONRPC_CPP_STUB_DEV_RPC_ETHFACE_H_

#include "ModularServer.h"

namespace dev
{
namespace rpc
{
class EthFace : public ServerInterface<EthFace>
{
public:
    EthFace()
    {
        this->bindAndAddMethod(jsonrpc::Procedure("eth_blockNumber", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_blockNumberI);
        this->bindAndAddMethod(jsonrpc::Procedure("eth_pbftView", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_pbftViewI);

        this->bindAndAddMethod(jsonrpc::Procedure("admin_peers", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::admin_peersI);

        this->bindAndAddMethod(jsonrpc::Procedure("eth_getBlockByHash", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getBlockByHashI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_getBlockByNumber", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getBlockByNumberI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_getTransactionByHash", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getTransactionByHashI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_getTransactionByBlockHashAndIndex", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getTransactionByBlockHashAndIndexI);
        this->bindAndAddMethod(jsonrpc::Procedure("eth_getTransactionByBlockNumberAndIndex",
                                   jsonrpc::PARAMS_BY_POSITION, jsonrpc::JSON_OBJECT, "param1",
                                   jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getTransactionByBlockNumberAndIndexI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_getTransactionReceipt", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_getTransactionReceiptI);
        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_pendingTransactions", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_pendingTransactionsI);
        this->bindAndAddMethod(jsonrpc::Procedure("eth_call", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_callI);

        this->bindAndAddMethod(
            jsonrpc::Procedure("eth_sendRawTransaction", jsonrpc::PARAMS_BY_POSITION,
                jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::eth_sendRawTransactionI);

        this->bindAndAddMethod(jsonrpc::Procedure("amop_topics", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::amop_topicsI);
        this->bindAndAddMethod(jsonrpc::Procedure("amop_setTopics", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::amop_setTopicsI);
        this->bindAndAddMethod(jsonrpc::Procedure("amop_sendMessage", jsonrpc::PARAMS_BY_POSITION,
                                   jsonrpc::JSON_OBJECT, "param1", jsonrpc::JSON_OBJECT, NULL),
            &dev::rpc::EthFace::amop_sendMessageI);
    }

    inline virtual void eth_blockNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_blockNumber(request);
    }
    inline virtual void eth_pbftViewI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_pbftView(request);
    }

    inline virtual void admin_peersI(const Json::Value& request, Json::Value& response)
    {
        response = this->admin_peers(request);
    }

    inline virtual void eth_getBlockByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getBlockByHash(request);
    }
    inline virtual void eth_getBlockByNumberI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getBlockByNumber(request);
    }

    inline virtual void eth_getTransactionByHashI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getTransactionByHash(request);
    }
    inline virtual void eth_getTransactionByBlockHashAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getTransactionByBlockHashAndIndex(request);
    }
    inline virtual void eth_getTransactionByBlockNumberAndIndexI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getTransactionByBlockNumberAndIndex(request);
    }
    inline virtual void eth_getTransactionReceiptI(
        const Json::Value& request, Json::Value& response)
    {
        response = this->eth_getTransactionReceipt(request);
    }
    inline virtual void eth_pendingTransactionsI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_pendingTransactions(request);
    }
    inline virtual void eth_callI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_call(request);
    }

    inline virtual void eth_sendRawTransactionI(const Json::Value& request, Json::Value& response)
    {
        response = this->eth_sendRawTransaction(request);
    }

    inline virtual void amop_topicsI(const Json::Value& request, Json::Value& response)
    {
        response = this->amop_topics(request);
    }
    inline virtual void amop_setTopicsI(const Json::Value& request, Json::Value& response)
    {
        response = this->amop_setTopics(request);
    }
    inline virtual void amop_sendMessageI(const Json::Value& request, Json::Value& response)
    {
        response = this->amop_sendMessage(request);
    }

    // consensus part
    virtual Json::Value eth_blockNumber(const Json::Value& requestJson) = 0;
    virtual Json::Value eth_pbftView(const Json::Value& requestJson) = 0;

    // p2p part
    virtual Json::Value admin_peers(const Json::Value& requestJson) = 0;

    // block part
    virtual Json::Value eth_getBlockByHash(const Json::Value& requestJson) = 0;
    virtual Json::Value eth_getBlockByNumber(const Json::Value& requestJson) = 0;

    // transaction part
    /// @return the information about a transaction requested by transaction hash.
    virtual Json::Value eth_getTransactionByHash(const Json::Value& requestJson) = 0;
    /// @return information about a transaction by block hash and transaction index position.
    virtual Json::Value eth_getTransactionByBlockHashAndIndex(const Json::Value& requestJson) = 0;
    /// @return information about a transaction by block number and transaction index position.
    virtual Json::Value eth_getTransactionByBlockNumberAndIndex(const Json::Value& requestJson) = 0;
    /// @return the receipt of a transaction by transaction hash.
    /// @note That the receipt is not available for pending transactions.
    virtual Json::Value eth_getTransactionReceipt(const Json::Value& requestJson) = 0;
    /// @return information about pendingTransactions.
    virtual Json::Value eth_pendingTransactions(const Json::Value& requestJson) = 0;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual Json::Value eth_call(const Json::Value& requestJson) = 0;

    // Creates new message call transaction or a contract creation for signed transactions.
    virtual Json::Value eth_sendRawTransaction(const Json::Value& requestJson) = 0;

    // amop part
    virtual Json::Value amop_topics(const Json::Value& requestJson) = 0;
    virtual Json::Value amop_setTopics(const Json::Value& requestJson) = 0;
    virtual Json::Value amop_sendMessage(const Json::Value& requestJson) = 0;
};

}  // namespace rpc
}  // namespace dev
#endif  // JSONRPC_CPP_STUB_DEV_RPC_ETHFACE_H_
