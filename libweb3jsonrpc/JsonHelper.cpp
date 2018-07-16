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
/** @file JsonHelper.cpp
 * @authors:
 *   Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "JsonHelper.h"

#include <libevmcore/Instruction.h>
#include <libethcore/SealEngine.h>
#include <libethereum/Client.h>
#include <libethereum/BlockQueue.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libwebthree/WebThree.h>
#include <libethcore/CommonJS.h>
#include <libethcore/ICAP.h>
#include <libwhisper/Message.h>
#include <libwhisper/WhisperHost.h>
#include <jsonrpccpp/common/exception.h>
#include <abi/SolidityExp.h>
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace eth;

namespace dev
{

Json::Value toJson(unordered_map<u256, u256> const& _storage)
{
	Json::Value res(Json::objectValue);
	for (auto i: _storage)
		res[toJS(i.first)] = toJS(i.second);
	return res;
}

Json::Value toJson(map<h256, pair<u256, u256>> const& _storage)
{
	Json::Value res(Json::objectValue);
	for (auto i: _storage)
		res[toJS(u256(i.second.first))] = toJS(i.second.second);
	return res;
}

Json::Value toJson(Address const& _address)
{
	return toJS(_address);
}

// ////////////////////////////////////////////////////////////////////////////////
// p2p
// ////////////////////////////////////////////////////////////////////////////////
namespace p2p
{

Json::Value toJson(p2p::PeerSessionInfo const& _p)
{
	//@todo localAddress
	//@todo protocols
	Json::Value ret;
	ret["id"] = _p.id.hex();
	ret["name"] = _p.clientVersion;
	ret["network"]["remoteAddress"] = _p.host + ":" + toString(_p.port);
	ret["lastPing"] = (int)chrono::duration_cast<chrono::milliseconds>(_p.lastPing).count();
	for (auto const& i: _p.notes)
		ret["notes"][i.first] = i.second;
	for (auto const& i: _p.caps)
		ret["caps"].append(i.first + "/" + toString((unsigned)i.second));
	return ret;
}

}

// ////////////////////////////////////////////////////////////////////////////////
// eth
// ////////////////////////////////////////////////////////////////////////////////

namespace eth
{

Json::Value toJson(dev::eth::BlockHeader const& _bi, SealEngineFace* _sealer)
{
	Json::Value res;
	if (_bi)
	{
		DEV_IGNORE_EXCEPTIONS(res["hash"] = toJS(_bi.hash()));
		res["parentHash"] = toJS(_bi.parentHash());
		res["sha3Uncles"] = toJS(_bi.sha3Uncles());
		res["author"] = toJS(_bi.author());
		res["stateRoot"] = toJS(_bi.stateRoot());
		res["transactionsRoot"] = toJS(_bi.transactionsRoot());
		res["receiptsRoot"] = toJS(_bi.receiptsRoot());
		res["number"] = toJS(_bi.number());
		res["gasUsed"] = toJS(_bi.gasUsed());
		res["gasLimit"] = toJS(_bi.gasLimit());
		res["extraData"] = toJS(_bi.extraData());
		res["logsBloom"] = toJS(_bi.logBloom());
		res["timestamp"] = toJS(_bi.timestamp());
		res["difficulty"] = toJS(_bi.difficulty());
		res["genIndex"] = toJS(_bi.genIndex());

		auto &nodeList = _bi.nodeList();
		res["minerNodeId"] = Json::Value();
		if(nodeList.size() > 0)
			res["minerNodeId"] = nodeList[_bi.genIndex().convert_to<int>()].hex();

		res["nodeList"] = Json::Value(Json::arrayValue);
		for(unsigned int i = 0; i < nodeList.size(); i++)
			res["nodeList"].append(nodeList[i].hex());
		// TODO: remove once JSONRPC spec is updated to use "author" over "miner".
		res["miner"] = toJS(_bi.author());
		if (_sealer)
			for (auto const& i: _sealer->jsInfo(_bi))
				res[i.first] = i.second;

	}
	return res;
}

Json::Value toJson(dev::eth::Transaction const& _t, std::pair<h256, unsigned> _location, BlockNumber _blockNumber)
{
	Json::Value res;
	if (_t)
	{
		res["hash"] = toJS(_t.sha3());
		res["input"] = toJS(_t.data());
		res["to"] = _t.isCreation() ? Json::Value() : toJS(_t.receiveAddress());
		res["from"] = toJS(_t.safeSender());
		res["gas"] = toJS(_t.gas());
		res["gasPrice"] = toJS(_t.gasPrice());
		res["nonce"] = toJS(_t.randomid());
		res["value"] = toJS(_t.value());
		res["blockHash"] = toJS(_location.first);
		res["transactionIndex"] = toJS(_location.second);
		res["blockNumber"] = toJS(_blockNumber);
		res["randomId"] = toJS(_t.randomid());
		//add operation field
		res["operation"] = _t.isCreation() ? Json::Value(Json::nullValue) : _t.cnsParams().toJsonObject();
	}
	return res;
}

Json::Value toJson(dev::eth::BlockHeader const& _bi, BlockDetails const& _bd, UncleHashes const& _us, Transactions const& _ts, SealEngineFace* _face)
{
	Json::Value res = toJson(_bi, _face);
	if (_bi)
	{
		res["size"] = toJS(_bd.rlp().size());
		res["totalDifficulty"] = toJS(_bd.totalDifficulty);
		res["uncles"] = Json::Value(Json::arrayValue);
		for (h256 h: _us)
			res["uncles"].append(toJS(h));
		res["transactions"] = Json::Value(Json::arrayValue);
		for (unsigned i = 0; i < _ts.size(); i++)
			res["transactions"].append(toJson(_ts[i], std::make_pair(_bi.hash(), i), (BlockNumber)_bi.number()));
	}
	return res;
}

Json::Value toJson(dev::eth::BlockHeader const& _bi, BlockDetails const& _bd, UncleHashes const& _us, TransactionHashes const& _ts, SealEngineFace* _face)
{
	Json::Value res = toJson(_bi, _face);
	if (_bi)
	{
		res["size"] = toJS(_bd.rlp().size());
		res["totalDifficulty"] = toJS(_bd.totalDifficulty);
		res["uncles"] = Json::Value(Json::arrayValue);
		for (h256 h: _us)
			res["uncles"].append(toJS(h));
		res["transactions"] = Json::Value(Json::arrayValue);
		for (h256 const& t: _ts)
			res["transactions"].append(toJS(t));
	}
	return res;
}

Json::Value toJson(dev::eth::TransactionSkeleton const& _t)
{
	Json::Value res;
	res["to"] = _t.creation ? Json::Value() : toJS(_t.to);
	res["from"] = toJS(_t.from);
	res["gas"] = toJS(_t.gas);
	res["gasPrice"] = toJS(_t.gasPrice);
	res["value"] = toJS(_t.value);
	res["data"] = toJS(_t.data, 32);
	return res;
}

Json::Value toJson(dev::eth::TransactionReceipt const& _t)
{
	Json::Value res;
	res["stateRoot"] = toJS(_t.stateRoot());
	res["gasUsed"] = toJS(_t.gasUsed());
	res["contractAddress"] = toJS(_t.contractAddress());//新增的
	res["bloom"] = toJS(_t.bloom());
	res["log"] = dev::toJson(_t.log());
	return res;
}

Json::Value toJson(dev::eth::LocalisedTransactionReceipt const& _t)
{
	Json::Value res;
	res["transactionHash"] = toJS(_t.hash());
	res["transactionIndex"] = _t.transactionIndex();
	res["blockHash"] = toJS(_t.blockHash());
	res["blockNumber"] = _t.blockNumber();
	res["cumulativeGasUsed"] = toJS(_t.gasUsed()); // TODO: check if this is fine
	res["gasUsed"] = toJS(_t.gasUsed());
	res["contractAddress"] = toJS(_t.contractAddress());
	res["logs"] = dev::toJson(_t.localisedLogs());
	return res;
}

Json::Value toJson(dev::eth::Transaction const& _t)
{
	Json::Value res;
	res["to"] = _t.isCreation() ? Json::Value() : toJS(_t.to());
	res["from"] = toJS(_t.from());
	res["gas"] = toJS(_t.gas());
	res["gasPrice"] = toJS(_t.gasPrice());
	res["value"] = toJS(_t.value());
	res["data"] = toJS(_t.data(), 32);
	res["nonce"] = toJS(_t.randomid());
	res["hash"] = toJS(_t.sha3(WithSignature));
	res["sighash"] = toJS(_t.sha3(WithoutSignature));
	res["r"] = toJS(_t.signature().r);
	res["s"] = toJS(_t.signature().s);
#if ETH_ENCRYPTTYPE
	res["pub"] = toJS(_t.signature().pub);
#else
	res["v"] = toJS(_t.signature().v);
#endif
	return res;
}

Json::Value toJson(dev::eth::LocalisedTransaction const& _t)
{
	Json::Value res;
	if (_t)
	{
		res["hash"] = toJS(_t.sha3());
		res["input"] = toJS(_t.data());
		res["to"] = _t.isCreation() ? Json::Value() : toJS(_t.receiveAddress());
		res["from"] = toJS(_t.safeSender());
		res["gas"] = toJS(_t.gas());
		res["gasPrice"] = toJS(_t.gasPrice());
		res["nonce"] = toJS(_t.randomid());
		res["value"] = toJS(_t.value());
		res["blockHash"] = toJS(_t.blockHash());
		res["transactionIndex"] = toJS(_t.transactionIndex());
		res["blockNumber"] = toJS(_t.blockNumber());
	}
	return res;
}

Json::Value toJson(dev::eth::NodeConnParams const& _t)
{
	Json::Value res;
	res["NodeId"] = _t._sNodeId;
	res["AgencyInfo"] = _t._sAgencyInfo;
	res["IP"] = _t._sIP;
	res["Port"] = _t._iPort;
	res["IdentityType"] = _t._iIdentityType;
	res["AgencyDesc"] = _t._sAgencyDesc;
	return res;
}

Json::Value toJson(dev::eth::LocalisedLogEntry const& _e)
{
	Json::Value res;

	if (_e.isSpecial)
		res = toJS(_e.special);
	else
	{
		res = toJson(static_cast<dev::eth::LogEntry const&>(_e));
		res["polarity"] = _e.polarity == BlockPolarity::Live ? true : false;
		if (_e.mined)
		{
			res["type"] = "mined";
			res["blockNumber"] = _e.blockNumber;
			res["blockHash"] = toJS(_e.blockHash);
			res["logIndex"] = _e.logIndex;
			res["transactionHash"] = toJS(_e.transactionHash);
			res["transactionIndex"] = _e.transactionIndex;
		}
		else
		{
			res["type"] = "pending";
			res["blockNumber"] = Json::Value(Json::nullValue);
			res["blockHash"] = Json::Value(Json::nullValue);
			res["logIndex"] = Json::Value(Json::nullValue);
			res["transactionHash"] = Json::Value(Json::nullValue);
			res["transactionIndex"] = Json::Value(Json::nullValue);
		}
	}
	return res;
}

Json::Value toJson(dev::eth::LogEntry const& _e)
{
	Json::Value res;
	res["data"] = toJS(_e.data);
	res["address"] = toJS(_e.address);
	res["topics"] = Json::Value(Json::arrayValue);
	for (auto const& t: _e.topics)
		res["topics"].append(toJS(t));
	return res;
}

Json::Value toJson(std::unordered_map<h256, dev::eth::LocalisedLogEntries> const& _entriesByBlock, vector<h256> const& _order)
{
	Json::Value res(Json::arrayValue);
	for (auto const& i: _order)
	{
		auto entries = _entriesByBlock.at(i);
		Json::Value currentBlock(Json::objectValue);
		LocalisedLogEntry entry = entries[0];
		if (entry.mined)
		{

			currentBlock["blockNumber"] = entry.blockNumber;
			currentBlock["blockHash"] = toJS(entry.blockHash);
			currentBlock["type"] = "mined";
		}
		else
			currentBlock["type"] = "pending";

		currentBlock["polarity"] = entry.polarity == BlockPolarity::Live ? true : false;
		currentBlock["logs"] = Json::Value(Json::arrayValue);

		for (LocalisedLogEntry const& e: entries)
		{
			Json::Value log(Json::objectValue);
			log["logIndex"] = e.logIndex;
			log["transactionIndex"] = e.transactionIndex;
			log["transactionHash"] = toJS(e.transactionHash);
			log["address"] = toJS(e.address);
			log["data"] = toJS(e.data);
			log["topics"] = Json::Value(Json::arrayValue);
			for (auto const& t: e.topics)
				log["topics"].append(toJS(t));

			currentBlock["logs"].append(log);
		}

		res.append(currentBlock);
	}

	return res;
}

Json::Value toJsonByBlock(LocalisedLogEntries const& _entries)
{
	vector<h256> order;
	unordered_map <h256, LocalisedLogEntries> entriesByBlock;

	for (dev::eth::LocalisedLogEntry const& e: _entries)
	{
		if (e.isSpecial) // skip special log
			continue;

		if (entriesByBlock.count(e.blockHash) == 0)
		{
			entriesByBlock[e.blockHash] = LocalisedLogEntries();
			order.push_back(e.blockHash);
		}

		entriesByBlock[e.blockHash].push_back(e);
	}

	return toJson(entriesByBlock, order);
}

void fromJsonGetParams(Json::Value const& _json, CnsParams &params)
{
	/*
     format：
	{
	"contract": "Hello",
	"func": "get",
	"version": "",
	"params": []
	}
	*/

	//name 
	if (_json.isMember("contract") && _json["contract"].isString())
	{
		params.strContractName = _json["contract"].asString();
	}
	else
	{
		ABI_EXCEPTION_THROW("invalid json request, contract param not find or not string format, _json=" + _json.toStyledString(), libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
	}

	//func
	if (_json.isMember("func") && _json["func"].isString())
	{
		params.strFunc = _json["func"].asString();
	}
	else
	{
		ABI_EXCEPTION_THROW("invalid json request, func param not find or not string format, _json=" + _json.toStyledString(), libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
	}
	//params
	if (_json.isMember("params") && _json["params"].isArray())
	{
		params.jParams = _json["params"];
	}
	else
	{
		ABI_EXCEPTION_THROW("invalid json request, params param not find or not string format, json=" + _json.toStyledString(), libabi::EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeInvalidArgument);
	}

	//version
	if (_json.isMember("version") && _json["version"].isString())
	{
		params.strVersion = _json["version"].asString();
	}
}

bool fromJsonGetParams(std::string const& _json, CnsParams &params)
{
	/*      
	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(_json, root, false))
	{
	return false;
	}

	fromJsonGetParams(root, params);
	return true;
	*/

	try
	{
		Json::Value root;
		Json::Reader reader;
		if (reader.parse(_json, root, false) && root.isObject())
		{
			LOG(TRACE) << "#fromJsonGetParams# begin, _json = " << _json;

			fromJsonGetParams(root, params);
			return true;
		}
	}
	catch (const libabi::AbiException &e)
	{
		throw e;
	}
	catch (...)
	{//other exception
		LOG(INFO) << "#fromJsonGetParams# other exception , _json = " << _json;
	}

	return false;


}

TransactionSkeleton toTransactionSkeleton(Json::Value const& _json)
{
	TransactionSkeleton ret;
	if (!_json.isObject() || _json.empty())
		return ret;

	if (!_json["from"].empty())
		ret.from = jsToAddress(_json["from"].asString());
	if ((!_json["to"].empty())&& (!_json["to"].asString().empty()) && _json["to"].asString() != "0x")
		ret.to = jsToAddress(_json["to"].asString());
	else
		ret.creation = true;

	if (!_json["value"].empty())
		ret.value = jsToU256(_json["value"].asString());

	if (!_json["gas"].empty())
		ret.gas = jsToU256(_json["gas"].asString());

	if (!_json["gasPrice"].empty())
		ret.gasPrice = jsToU256(_json["gasPrice"].asString());

	if (!_json["data"].empty() && _json["data"].isString())							// ethereum.js has preconstructed the data array
		ret.data = jsToBytes(_json["data"].asString(), OnFailed::Throw);

	if (!_json["code"].empty())
		ret.data = jsToBytes(_json["code"].asString(), OnFailed::Throw);

	if (!_json["randomid"].empty())
		ret.randomid = jsToU256(_json["randomid"].asString());

	//add blocklimit params
	if (!_json["blockLimit"].empty())
		ret.blockLimit = jsToU256(_json["blockLimit"].asString());

	//in CNS, data is json object
	if (!_json["data"].empty() && _json["data"].isObject())
		ret.jData = _json["data"];

	//add version
	if (!_json["version"].empty())
		ret.strVersion = _json["version"].asString();

	//in CNS, add contract name
	if (!_json["contractName"].empty())
		ret.strContractName = _json["contractName"].asString();

	//keep type
	if (!_json["type"].empty())
		ret.type = jsToU256(_json["type"].asString());

	return ret;
}

dev::eth::LogFilter toLogFilter(Json::Value const& _json)
{
	dev::eth::LogFilter filter;
	if (!_json.isObject() || _json.empty())
		return filter;

	// check only !empty. it should throw exceptions if input params are incorrect
	if (!_json["fromBlock"].empty())
		filter.withEarliest(jsToFixed<32>(_json["fromBlock"].asString()));
	if (!_json["toBlock"].empty())
		filter.withLatest(jsToFixed<32>(_json["toBlock"].asString()));
	if (!_json["address"].empty())
	{
		if (_json["address"].isArray())
			for (auto i : _json["address"])
				filter.address(jsToAddress(i.asString()));
		else
			filter.address(jsToAddress(_json["address"].asString()));
	}
	if (!_json["topics"].empty())
		for (unsigned i = 0; i < _json["topics"].size(); i++)
		{
			if (_json["topics"][i].isArray())
			{
				for (auto t: _json["topics"][i])
					if (!t.isNull())
						filter.topic(i, jsToFixed<32>(t.asString()));
			}
			else if (!_json["topics"][i].isNull()) // if it is anything else then string, it should and will fail
				filter.topic(i, jsToFixed<32>(_json["topics"][i].asString()));
		}
	return filter;
}

// TODO: this should be removed once we decide to remove backward compatibility with old log filters
dev::eth::LogFilter toLogFilter(Json::Value const& _json, Interface const& _client)	// commented to avoid warning. Uncomment once in use @ PoC-7.
{
	dev::eth::LogFilter filter;
	if (!_json.isObject() || _json.empty())
		return filter;

	// check only !empty. it should throw exceptions if input params are incorrect
	if (!_json["fromBlock"].empty())
		filter.withEarliest(_client.hashFromNumber(jsToBlockNumber(_json["fromBlock"].asString())));
	if (!_json["toBlock"].empty())
		filter.withLatest(_client.hashFromNumber(jsToBlockNumber(_json["toBlock"].asString())));
	if (!_json["address"].empty())
	{
		if (_json["address"].isArray())
			for (auto i : _json["address"])
				filter.address(jsToAddress(i.asString()));
		else
			filter.address(jsToAddress(_json["address"].asString()));
	}
	if (!_json["topics"].empty())
		for (unsigned i = 0; i < _json["topics"].size(); i++)
		{
			if (_json["topics"][i].isArray())
			{
				for (auto t: _json["topics"][i])
					if (!t.isNull())
						filter.topic(i, jsToFixed<32>(t.asString()));
			}
			else if (!_json["topics"][i].isNull()) // if it is anything else then string, it should and will fail
				filter.topic(i, jsToFixed<32>(_json["topics"][i].asString()));
		}
	return filter;
}

}

// ////////////////////////////////////////////////////////////////////////////////////
// shh
// ////////////////////////////////////////////////////////////////////////////////////

namespace shh
{

Json::Value toJson(h256 const& _h, shh::Envelope const& _e, shh::Message const& _m)
{
	Json::Value res;
	res["hash"] = toJS(_h);
	res["expiry"] = toJS(_e.expiry());
	res["sent"] = toJS(_e.sent());
	res["ttl"] = toJS(_e.ttl());
	res["workProved"] = toJS(_e.workProved());
	res["topics"] = Json::Value(Json::arrayValue);
	for (auto const& t: _e.topic())
		res["topics"].append(toJS(t));
	res["payload"] = toJS(_m.payload());
	res["from"] = toJS(_m.from());
	res["to"] = toJS(_m.to());
	return res;
}

shh::Message toMessage(Json::Value const& _json)
{
	shh::Message ret;
	if (!_json["from"].empty())
		ret.setFrom(jsToPublic(_json["from"].asString()));
	if (!_json["to"].empty())
		ret.setTo(jsToPublic(_json["to"].asString()));
	if (!_json["payload"].empty())
		ret.setPayload(jsToBytes(_json["payload"].asString()));
	return ret;
}

shh::Envelope toSealed(Json::Value const& _json, shh::Message const& _m, Secret const& _from)
{
	unsigned ttl = 50;
	unsigned workToProve = 50;
	shh::BuildTopic bt;

	if (!_json["ttl"].empty())
		ttl = jsToInt(_json["ttl"].asString());

	if (!_json["workToProve"].empty())
		workToProve = jsToInt(_json["workToProve"].asString());

	if (!_json["topics"].empty())
		for (auto i: _json["topics"])
		{
			if (i.isArray())
			{
				for (auto j: i)
					if (!j.isNull())
						bt.shift(jsToBytes(j.asString()));
			}
			else if (!i.isNull()) // if it is anything else then string, it should and will fail
				bt.shift(jsToBytes(i.asString()));
		}

	return _m.seal(_from, bt, ttl, workToProve);
}

pair<shh::Topics, Public> toWatch(Json::Value const& _json)
{
	shh::BuildTopic bt;
	Public to;

	if (!_json["to"].empty())
		to = jsToPublic(_json["to"].asString());

	if (!_json["topics"].empty())
		for (auto i: _json["topics"])
			bt.shift(jsToBytes(i.asString()));

	return make_pair(bt, to);
}

}

// ////////////////////////////////////////////////////////////////////////////////////
// rpc
// ////////////////////////////////////////////////////////////////////////////////////

namespace rpc
{
h256 h256fromHex(string const& _s)
{
	try
	{
		return h256(_s);
	}
	catch (boost::exception const&)
	{
		throw jsonrpc::JsonRpcException("Invalid hex-encoded string: " + _s);
	}
}
}

}
