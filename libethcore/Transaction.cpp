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
/** @file TransactionBase.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include <abi/ContractAbiMgr.h>
#include <abi/SolidityTools.h>
#include <abi/SolidityCoder.h>
#include <libdevcore/vector_ref.h>
#include <libdevcore/easylog.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>
#include <libevmcore/EVMSchedule.h>
#include <libethcore/Exceptions.h>
#include <libethcore/CommonJS.h>
#include <libweb3jsonrpc/JsonHelper.h>

#include "Transaction.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

u256 TransactionBase::maxGas = 30000000; 

TransactionBase::TransactionBase(TransactionSkeleton const& _ts, Secret const& _s):
	m_type(_ts.creation ? ContractCreation : MessageCall),
	m_randomid(_ts.randomid),
	m_value(_ts.value),
	m_receiveAddress(_ts.to),
	m_gasPrice(_ts.gasPrice),
	m_gas(_ts.gas),
	m_blockLimit(_ts.blockLimit),
	m_data(_ts.data),
	m_sender(_ts.from)
{
	if (_s)
		sign(_s);
}

TransactionBase::TransactionBase(bytesConstRef _rlpData, CheckTransaction _checkSig)
{
	int field = 0;
	RLP rlp(_rlpData);
	try
	{
		transactionRLPDecode(_rlpData);
		if (_checkSig >= CheckTransaction::Cheap && !m_vrs.isValid()) 
		{
			BOOST_THROW_EXCEPTION(InvalidSignature());
		}

		if (_checkSig == CheckTransaction::Everything) {
			m_sender = sender();
		}
	}
	catch (Exception& _e)
	{
		_e << errinfo_name("invalid transaction format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
		throw;
	}
}

void TransactionBase::transactionRLPDecode(bytesConstRef _rlp)
{
	RLP rlp(_rlp);
	if (!rlp.isList())
		BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("transaction RLP must be a list"));

	size_t rlpItemCount = rlp.itemCount();
	if (rlpItemCount == 10)
	{
		transactionRLPDecode10Ele(rlp);
	}
	else if (rlpItemCount == 13)
	{
		transactionRLPDecode13Ele(rlp);
	}
	else
	{
		BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("invalid fields in the transaction RLP"));
	}
}

void TransactionBase::transactionRLPDecode10Ele(const RLP &rlp)
{
	int index = 0;
	m_randomid       = rlp[index++].toInt<u256>(); // 0 
	m_gasPrice       = rlp[index++].toInt<u256>(); // 1
	m_gas            = rlp[index++].toInt<u256>(); // 2
	m_blockLimit     = rlp[index++].toInt<u256>(); // 3
	m_receiveAddress = rlp[index].isEmpty() ? Address() : rlp[index].toHash<Address>(RLP::VeryStrict); // 4
	index++;  
	m_value          = rlp[index++].toInt<u256>(); // 5
	auto dataRLP     = rlp[index++];               // 6
	m_data           = dataRLP.toBytes();    

#if ETH_ENCRYPTTYPE
	h512 pub         = rlp[index++].toInt<u512>(); // 7
	h256 r           = rlp[index++].toInt<u256>(); // 8
	h256 s           = rlp[index++].toInt<u256>(); // 9
	m_vrs = SignatureStruct{ r, s, pub };
#else
	byte v           = rlp[index++].toInt<byte>(); // 7
	h256 r           = rlp[index++].toInt<u256>(); // 8
	h256 s           = rlp[index++].toInt<u256>(); // 9

	if (v > 36)
		m_chainId = (v - 35) / 2;
	else if (v == 27 || v == 28)
		m_chainId = -4;
	else
		BOOST_THROW_EXCEPTION(InvalidSignature());

	v = v - (m_chainId * 2 + 35);
	m_vrs = SignatureStruct{ r, s, v };
#endif
	Json::Value _json;
	if (isUTXOTx(dataRLP.toString(), _json))
	{
		// is UTXO transcation or not
		parseUTXOJson(_json);
		m_type = (m_receiveAddress == Address() ? ContractCreation : MessageCall);
	}
	else 
	{
		//判断是否是CNS调用
		auto isOldCNS = fromJsonGetParams(dataRLP.toString(), m_cnsParams);
		if (isOldCNS)
		{
			m_type            = MessageCall;
			m_transactionType = CNSOldTransaction;

			LOG(TRACE) << "[CNSOldTransaction] cncName|method|cnsVer|params=" 
				<< m_cnsParams.strContractName << "|"
				<< m_cnsParams.strFunc << "|"
				<< m_cnsParams.strVersion << "|"
				<< m_cnsParams.jParams.toStyledString();
		}
		else
		{
			m_type            = (m_receiveAddress == Address() ? ContractCreation : MessageCall);
			m_transactionType = DefaultTransaction;

			//LOG(TRACE) << "[CNSDefaultTransaction] to address = " << m_receiveAddress;
		}
	}
}

bool TransactionBase::isUTXOTx(const std::string& strJson, Json::Value& _json)
{
	try 
	{
		Json::Reader reader;
		if (reader.parse(strJson, _json, false) && 
			_json.isMember("utxotype") && _json["utxotype"].isInt())
		{
			LOG(TRACE) << "TransactionBase::TransactionBase UTXO data:" << strJson;
			return true;
		}
	}
	catch (...)
	{
		LOG(TRACE) << "TransactionBase::isUTXOTx exception , data = " << strJson;
	}

	return false;
}

void TransactionBase::transactionRLPDecode13Ele(const RLP &rlp)
{
	int index = 0;
	
	m_randomid        = rlp[index++].toInt<u256>(); //0
	m_gasPrice        = rlp[index++].toInt<u256>(); //1
	m_gas             = rlp[index++].toInt<u256>(); //2
	m_blockLimit      = rlp[index++].toInt<u256>(); //3
	m_receiveAddress  = rlp[index].isEmpty() ? Address() : rlp[index].toHash<Address>(RLP::VeryStrict); //4
	m_type            = rlp[index++].isEmpty() ? ContractCreation : MessageCall;
	m_value           = rlp[index++].toInt<u256>(); //5
	if (!rlp[index].isData())
		BOOST_THROW_EXCEPTION(InvalidTransactionFormat() << errinfo_comment("transaction data RLP must be an array"));
	m_data            = rlp[index++].toBytes();     // 6
	m_strCNSName      = rlp[index++].toString();    // 7
	m_strCNSVer       = rlp[index++].toString();    // 8
	m_cnsType         = rlp[index++].toInt<u256>(); // 9

#if ETH_ENCRYPTTYPE
	h512 pub          = rlp[index++].toInt<u512>(); // 10
	h256 r            = rlp[index++].toInt<u256>(); // 11
	h256 s            = rlp[index++].toInt<u256>(); // 12
	m_vrs             = SignatureStruct{ r, s, pub };
#else
	byte v            = rlp[index++].toInt<byte>(); // 10
	h256 r            = rlp[index++].toInt<u256>(); // 11
	h256 s            = rlp[index++].toInt<u256>(); // 12

	if (v > 36)
		m_chainId = (v - 35) / 2;
	else if (v == 27 || v == 28)
		m_chainId = -4;
	else
		BOOST_THROW_EXCEPTION(InvalidSignature());

	v = v - (m_chainId * 2 + 35);
	m_vrs             = SignatureStruct{ r, s, v };
#endif
	m_transactionType = CNSNewTransaction;
	m_type            = (m_receiveAddress == Address() && m_strCNSName.empty()) ? ContractCreation : MessageCall;

	LOG(TRACE) << "[CNSNewTransaction] cnsName|cnsVersion|isNewCNS|m_type="
		<< m_strCNSName << "|"
		<< m_strCNSVer << "|"
		<< isNewCNS() << "|"
		<< m_type;
}

const CnsParams &TransactionBase::cnsParams() const
{
	try
	{
		if (isNewCNS())
		{
			libabi::SolidityAbi abi;
			auto rVectorString = libabi::SolidityTools::splitString(m_strCNSName, libabi::SolidityTools::CNS_SPLIT_STRING);
			std::string contract = (!rVectorString.empty() ? rVectorString[0] : "");
			std::string version = (rVectorString.size() > 1 ? rVectorString[1] : "");
			libabi::ContractAbiMgr::getInstance()->getContractAbi(contract, version, abi);
			libabi::SolidityCoder::getInstance()->getCNSParams(abi, m_cnsParams, toHex(m_data));
		}
		else if (isDefault())
		{
			libabi::SolidityAbi abi;
			libabi::ContractAbiMgr::getInstance()->getContractAbiByAddr("0x" + m_receiveAddress.hex(), abi);
			libabi::SolidityCoder::getInstance()->getCNSParams(abi, m_cnsParams, toHex(m_data));
		}
	}
	catch (const std::exception& e)
	{
		LOG(DEBUG) << "# cnsParams , what msg=" << e.what();
	}

	return m_cnsParams;
}

void TransactionBase::doGetCNSparams() const
{
	if (m_isGetCNSParams)
	{
		return;
	}

	if (isOldCNS())
	{
		auto d = libabi::ContractAbiMgr::getInstance()->getAddrAndDataInfo(m_cnsParams.strContractName, m_cnsParams.strFunc, m_cnsParams.strVersion, m_cnsParams.jParams);
		m_addressGetByCNS = d.first;
		m_dataGetByCNS = d.second;
		LOG(TRACE) << "CNSOldTransaction # constract = " << m_cnsParams.strContractName
			<< " ,version = " << m_cnsParams.strVersion
			<< " ,address = " << m_addressGetByCNS
			;
	}
	else if (isNewCNS())
	{
		auto rVectorString = libabi::SolidityTools::splitString(m_strCNSName, libabi::SolidityTools::CNS_SPLIT_STRING);
		std::string contract = (!rVectorString.empty() ? rVectorString[0] : "");
		std::string version = (rVectorString.size() > 1 ? rVectorString[1] : "");

		libabi::SolidityAbi abi;
		libabi::ContractAbiMgr::getInstance()->getContractAbi0(contract, version, abi);
		m_addressGetByCNS = dev::jsToAddress(abi.getAddr());
		LOG(TRACE) << "CNSNewTransaction # constract = " << m_strCNSName
			<< " ,version = " << m_strCNSVer
			<< " ,address = " << m_addressGetByCNS
			;
	}

	m_isGetCNSParams = true;
}

Address TransactionBase::receiveAddress() const
{
	doGetCNSparams();
	return isDefault() ? m_receiveAddress : m_addressGetByCNS;
}
							   /// Synonym for receiveAddress().
Address TransactionBase::to() const
{
	doGetCNSparams();
	return isDefault() ? m_receiveAddress : m_addressGetByCNS;
}

bytes const&  TransactionBase::data() const
{
	doGetCNSparams();
	return (isOldCNS() ? m_dataGetByCNS : m_data);
}

Address const& TransactionBase::safeSender() const noexcept
{
	try
	{
		return sender();
	}
	catch (...)
	{
		return ZeroAddress;
	}
}

Address const& TransactionBase::sender() const
{
	if (!m_sender)
	{
		auto p = recover(m_vrs, sha3(WithoutSignature));

		if (!p)
			BOOST_THROW_EXCEPTION(InvalidSignature());
		m_sender = right160(dev::sha3(bytesConstRef(p.data(), sizeof(p))));

	}

	return m_sender;
}

void TransactionBase::sign(Secret const& _priv)
{
	auto sig = dev::sign(_priv, sha3(WithoutSignature));
	SignatureStruct sigStruct = *(SignatureStruct const*)&sig;
	if (sigStruct.isValid())
		m_vrs = sigStruct;
}

void TransactionBase::streamRLP(RLPStream& _s, IncludeSignature _sig, bool _forEip155hash) const
{
	if (m_type == NullTransaction)
		return;

	_s.appendList((_sig || _forEip155hash ? 3 : 0) + (isCNSNewTransaction() ? 10 : 7));
	_s << m_randomid << m_gasPrice << m_gas << m_blockLimit ; 
	if (m_receiveAddress==Address())
	{
		_s << "";
	}
	else
	{
		_s << m_receiveAddress;
	}

	_s << m_value;
	_s << m_data;

	if (isCNSNewTransaction())
	{
		_s << m_strCNSName;
		_s << m_strCNSVer;
		_s << m_cnsType;
	}

#if ETH_ENCRYPTTYPE
	if (_sig)
	{
		_s << (u512)m_vrs.pub << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
#else
	if (_sig)
	{
		int vOffset = m_chainId * 2 + 35;
		_s << (m_vrs.v + vOffset) << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
#endif
}

void TransactionBase::streamRLP(std::stringstream& _s, IncludeSignature _sig, bool _forEip155hash) const {
	if (m_type == NullTransaction)
		return;

	_s << m_randomid << m_gasPrice << m_gas << m_blockLimit; 
	if (m_receiveAddress == Address())
	{
		_s << "";
	}
	else
	{
		_s << m_receiveAddress;
	}

	_s << m_value;
	_s << toHex(m_data);

	if (isCNSNewTransaction())
	{
		_s << m_strCNSName;
		_s << m_strCNSVer;
		_s << m_cnsType;
	}
#if ETH_ENCRYPTTYPE
	if (_sig)
	{
		_s << (u512)m_vrs.pub << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
#else
	if (_sig)
	{
		int vOffset = m_chainId * 2 + 35;
		_s << (m_vrs.v + vOffset) << (u256)m_vrs.r << (u256)m_vrs.s;
	}
	else if (_forEip155hash)
		_s << m_chainId << 0 << 0;
#endif
}

static const u256 c_secp256k1n("115792089237316195423570985008687907852837564279074904382605163141518161494337");

void TransactionBase::checkLowS() const
{
#ifndef ETH_ENCRYPTTYPE
	if (m_vrs.s > c_secp256k1n / 2)
		BOOST_THROW_EXCEPTION(InvalidSignature());
#endif
}

void TransactionBase::checkChainId(int chainId) const
{
	if (m_chainId != chainId && m_chainId != -4)
		BOOST_THROW_EXCEPTION(InvalidSignature());
}

bigint TransactionBase::gasRequired(bool _contractCreation, bytesConstRef _data, EVMSchedule const& _es, u256 const& _gas)
{
	bigint ret = (_contractCreation ? _es.txCreateGas : _es.txGas) + _gas;
	for (auto i : _data)
		ret += i ? _es.txDataNonZeroGas : _es.txDataZeroGas;
	return ret;
}

h256 TransactionBase::sha3(IncludeSignature _sig) const
{
	if (_sig == WithSignature && m_hashWith)
		return m_hashWith;

	RLPStream s;
	//std::stringstream s;
	streamRLP(s, _sig, m_chainId > 0 && _sig == WithoutSignature);

	auto ret = dev::sha3(s.out());
	//auto ret = dev::sha3(s.str());
	if (_sig == WithSignature)
		m_hashWith = ret;
	return ret;
}

void TransactionBase::checkUTXOTransaction(UTXOModel::UTXOMgr* _pUTXOMgr) const
{
	if (UTXOType::InitTokens == m_utxoType)
	{
		_pUTXOMgr->checkInitTokens(m_utxoTxOut);
	}
	else if (UTXOType::SendSelectedTokens == m_utxoType)
	{
		_pUTXOMgr->checkSendTokens(m_sender, m_utxoTxIn, m_utxoTxOut);
	}
}

bool TransactionBase::isUTXOEvmTx() const
{
	return m_utxoEvmTx;
}

void TransactionBase::parseUTXOJson(const Json::Value& _json)
{
	m_utxoType = UTXOType(_json["utxotype"].asInt());
	if (_json.isMember("txin") && _json["txin"].isArray())
	{
		Json::Value tx_in_json = _json["txin"];
		for (uint index = 0; index < tx_in_json.size(); index++)
		{
			Json::Value per = tx_in_json[index];
			UTXOModel::UTXOTxIn in;
			in.tokenKey = per["tokenkey"].asString();
			in.callFuncAndParams = per["callfuncandparams"].asString();
			in.exeFuncAndParams = per["exefuncandparams"].asString();
			in.detail = per["desdetail"].asString();
			m_utxoTxIn.push_back(in);
			if (in.callFuncAndParams.length() > 0)
			{
				m_utxoEvmTx = true;
			}
		}
	}
	if (_json.isMember("txout") && _json["txout"].isArray())
	{
		Json::Value tx_out_json = _json["txout"];
		for (uint index = 0; index < tx_out_json.size(); index++)
		{
			Json::Value per = tx_out_json[index];
			UTXOModel::UTXOTxOut out;
			out.to = per["to"].asString();
			out.value = jsToU256(per["value"].asString());
			out.checkType = per["checktype"].asString();
			out.initContract = (per["initcontract"].empty()) ? Address(0) : jsToAddress(per["initcontract"].asString());
			out.initFuncAndParams = per["initfuncandparams"].asString();
			out.validationContract = (per["validationcontract"].empty()) ? Address(0) : jsToAddress(per["validationcontract"].asString());
			out.detail = per["oridetail"].asString();
			m_utxoTxOut.push_back(out);
			if (out.validationContract != Address(0) || 
				out.initContract != Address(0))
			{
				m_utxoEvmTx = true;
			}
		}
	}
	// "m_value" can be obtained from the contract's "msg.value()".
}

/*
h256 TransactionBase::sha32(IncludeSignature _sig) const
{
	RLPStream s;
	streamRLP(s, _sig, m_chainId > 0 && _sig == WithoutSignature);

	auto ret = dev::sha3(s.out());
	return ret;
}
*/
