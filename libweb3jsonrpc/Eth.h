/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Eth.h
 * @authors:
 *   Gav Wood <i@gavwood.com>
 *   Marek Kotewicz <marek@ethdev.com>
 * @date 2014
 */

#pragma once

#include <memory>
#include <iostream>

#include <jsonrpccpp/server.h>
#include <jsonrpccpp/common/exception.h>
#include <libdevcrypto/Common.h>
#include <libethereum/Client.h>
#include <UTXO/UTXOData.h>

#include "EthFace.h"
#include "SessionManager.h"

namespace dev
{
class NetworkFace;
class KeyPair;
namespace eth
{
class AccountHolder;
struct TransactionSkeleton;
class Interface;
}
namespace shh
{
class Interface;
}

extern const unsigned SensibleHttpThreads;
extern const unsigned SensibleHttpPort;

}

namespace dev
{

namespace rpc
{

/**
 * @brief JSON-RPC api implementation
 */
class Eth: public dev::rpc::EthFace
{
public:
	Eth(eth::Interface& _eth, eth::AccountHolder& _ethAccounts);

	virtual RPCModules implementedModules() const override
	{
		return RPCModules{RPCModule{"eth", "1.0"}};
	}

	eth::AccountHolder const& ethAccounts() const { return m_ethAccounts; }

	virtual std::string eth_protocolVersion() override;
	virtual std::string eth_hashrate() override;
	virtual std::string eth_coinbase() override;
	virtual bool eth_mining() override;
	virtual std::string eth_gasPrice() override;
	virtual Json::Value eth_accounts() override;
	virtual std::string eth_blockNumber()override;
	virtual std::string eth_pbftView()override;
	virtual std::string eth_pbftToView()override;
    virtual Json::Value eth_getProofMerkle(string const& _blockHash, string const& _transactionIndex)override;
	virtual std::string eth_getBalance(std::string const& _address, std::string const& _blockNumber) override;
	virtual std::string eth_getStorageAt(std::string const& _address, std::string const& _position, std::string const& _blockNumber) override;
	virtual std::string eth_getStorageRoot(std::string const& _address, std::string const& _blockNumber) override;
	virtual std::string eth_getTransactionCount(std::string const& _address, std::string const& _blockNumber) override;
	//virtual std::string eth_pendingTransactions() override;
	virtual std::string eth_pendingTransactionsNum() override;
	virtual Json::Value eth_pendingTransactions() override;
	virtual Json::Value eth_getBlockTransactionCountByHash(std::string const& _blockHash) override;
	virtual Json::Value eth_getBlockTransactionCountByNumber(std::string const& _blockNumber) override;
	virtual Json::Value eth_getUncleCountByBlockHash(std::string const& _blockHash) override;
	virtual Json::Value eth_getUncleCountByBlockNumber(std::string const& _blockNumber) override;
	virtual std::string eth_getCode(std::string const& _address, std::string const& _blockNumber) override;
	virtual std::string eth_sendTransaction(Json::Value const& _json) override;
	virtual std::string eth_call(Json::Value const& _json, std::string const& _blockNumber) override;

	//jsonCall
	virtual Json::Value eth_jsonCall(Json::Value const& _json, std::string const& _blockNumber) override;

	virtual std::string eth_estimateGas(Json::Value const& _json) override;
	virtual bool eth_flush() override;
	virtual Json::Value eth_getBlockByHash(std::string const& _blockHash, bool _includeTransactions) override;
	virtual Json::Value eth_getBlockByNumber(std::string const& _blockNumber, bool _includeTransactions) override;
	virtual Json::Value eth_getTransactionByHash(std::string const& _transactionHash) override;
	virtual Json::Value eth_getTransactionByBlockHashAndIndex(std::string const& _blockHash, std::string const& _transactionIndex) override;
	virtual Json::Value eth_getTransactionByBlockNumberAndIndex(std::string const& _blockNumber, std::string const& _transactionIndex) override;
	virtual Json::Value eth_getTransactionReceipt(std::string const& _transactionHash) override;
	virtual Json::Value eth_getUncleByBlockHashAndIndex(std::string const& _blockHash, std::string const& _uncleIndex) override;
	virtual Json::Value eth_getUncleByBlockNumberAndIndex(std::string const& _blockNumber, std::string const& _uncleIndex) override;
	virtual std::string eth_newFilter(Json::Value const& _json) override;
	virtual std::string eth_newFilterEx(Json::Value const& _json) override;
	virtual std::string eth_newBlockFilter() override;
	virtual std::string eth_newPendingTransactionFilter() override;
	virtual bool eth_uninstallFilter(std::string const& _filterId) override;
	virtual Json::Value eth_getFilterChanges(std::string const& _filterId) override;
	virtual Json::Value eth_getFilterChangesEx(std::string const& _filterId) override;
	virtual Json::Value eth_getFilterLogs(std::string const& _filterId) override;
	virtual Json::Value eth_getFilterLogsEx(std::string const& _filterId) override;
	virtual Json::Value eth_getLogs(Json::Value const& _json) override;
	virtual Json::Value eth_getLogsEx(Json::Value const& _json) override;
	virtual Json::Value eth_getWork() override;
	virtual bool eth_submitWork(std::string const& _nonce, std::string const&, std::string const& _mixHash) override;
	virtual bool eth_submitHashrate(std::string const& _hashes, std::string const& _id) override;
	virtual std::string eth_register(std::string const& _address) override;
	virtual bool eth_unregister(std::string const& _accountId) override;
	virtual Json::Value eth_fetchQueuedTransactions(std::string const& _accountId) override;
	virtual std::string eth_signTransaction(Json::Value const& _transaction) override;
	virtual Json::Value eth_inspectTransaction(std::string const& _rlp) override;
	virtual std::string eth_sendRawTransaction(std::string const& _rlp) override;
	virtual bool eth_notePassword(std::string const&) override { return false; }
	virtual Json::Value eth_syncing() override;

	void setTransactionDefaults(eth::TransactionSkeleton& _t);

	virtual std::string eth_unverifiedBlockQueueSize()override;
	virtual std::string eth_verifiedBlockQueueSize()override;

	virtual std::string eth_unverifiedTransactionsQueueSize()override;
	virtual std::string eth_verifiedTransactionsQueueSize()override;

	bool isDefaultCall(const TransactionSkeleton &t, CnsParams &params, const Json::Value &_json);
	bool isOldCNSCall(const TransactionSkeleton &t, CnsParams &params ,const Json::Value &_json);
	bool isNewCNSCall(const TransactionSkeleton &t);

	void eth_sendTransactionOldCNSSetParams(TransactionSkeleton &t);
	void eth_sendTransactionNewCNSSetParams(TransactionSkeleton &t);

	std::string eth_callDefault(TransactionSkeleton &t, std::string const& _blockNumber);
	std::string eth_callOldCNS(TransactionSkeleton &t,const CnsParams &params,std::string const& _blockNumber);
	std::string eth_callNewCNS(TransactionSkeleton &t, std::string const& _blockNumber);

	//CNS
	virtual std::string eth_getCodeCNS(std::string const& strContractName, std::string const& _blockNumber) override;
	virtual std::string eth_getBalanceCNS(std::string const& strContractName, std::string const& _blockNumber) override;
	virtual std::string eth_getStorageAtCNS(std::string const& strContractName, std::string const& _position, std::string const& _blockNumber) override;
	virtual std::string eth_getTransactionCountCNS(std::string const& strContractName, std::string const& _blockNumber) override;

	//ZKG
	virtual Json::Value eth_getCmByRange(Json::Value const& range) override;
	virtual Json::Value eth_getGovDataByRange(Json::Value const& range) override;
protected:

	eth::Interface* client() { return &m_eth; }

	eth::Interface& m_eth;
	eth::AccountHolder& m_ethAccounts;

	bool isUTXOTx(Json::Value const& _json);
	string utxoCall(Json::Value const& _json);
	bool getAccount(Json::Value const& utxoData, dev::Address& account, std::string& ret);
	bool getValue(Json::Value const& utxoData, dev::u256& value, std::string& ret);
	UTXOModel::QueryUTXOParam getQueryParam(Json::Value const& utxoData);
	string utxo_call_registerAccount(Json::Value const& utxoData);
	string utxo_call_getToken(Json::Value const& utxoData);
	string utxo_call_getTx(Json::Value const& utxoData);
	string utxo_call_getVault(Json::Value const& utxoData);
	string utxo_call_tokenTracking(Json::Value const& utxoData);
	string utxo_call_selectTokens(Json::Value const& utxoData);
	string utxo_call_getBalance(Json::Value const& utxoData);
};

}
} //namespace dev
