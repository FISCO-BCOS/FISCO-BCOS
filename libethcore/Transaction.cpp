/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Transaction.cpp
 * @author Gav Wood <i@gavwood.com>, chaychen, asherli
 * @date 2018
 */

#include "Transaction.h"
#include "EVMSchedule.h"
#include "Exceptions.h"
#include <libdevcore/vector_ref.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Exceptions.h>

using namespace std;
using namespace dev;
// using namespace dev::crypto;
using namespace dev::eth;
Transaction::Transaction(bytesConstRef _rlpData, CheckTransaction _checkSig)
{
    m_rpcCallback = nullptr;
    decode(_rlpData, _checkSig);
}

void Transaction::decode(bytesConstRef tx_bytes, CheckTransaction _checkSig)
{
    RLP const rlp(tx_bytes);
    decode(rlp, _checkSig);
}

void Transaction::decode(RLP const& rlp, CheckTransaction _checkSig)
{
    try
    {
        if (!rlp.isList())
            BOOST_THROW_EXCEPTION(
                InvalidTransactionFormat() << errinfo_comment("transaction RLP must be a list"));

        m_nonce = rlp[0].toInt<u256>();
        m_gasPrice = rlp[1].toInt<u256>();
        m_gas = rlp[2].toInt<u256>();
        m_blockLimit = rlp[3].toInt<u256>();
        m_type = rlp[4].isEmpty() ? ContractCreation : MessageCall;
        m_receiveAddress = rlp[4].isEmpty() ? Address() : rlp[4].toHash<Address>(RLP::VeryStrict);
        m_value = rlp[5].toInt<u256>();

        if (!rlp[6].isData())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("transaction data RLP must be an array"));

        m_data = rlp[6].toBytes();

        VType const v = rlp[7].toInt<TYPE_V>();  // 7
        h256 const r = rlp[8].toInt<u256>();     // 8
        h256 const s = rlp[9].toInt<u256>();     // 9

        m_vrs = SignatureStruct(v, r, s);

        if (_checkSig >= CheckTransaction::Cheap && !m_vrs->isValid())
            BOOST_THROW_EXCEPTION(InvalidSignature());

        if (_checkSig == CheckTransaction::Everything)
            m_sender = sender();
    }
    catch (Exception& _e)
    {
        _e << errinfo_name(
            "invalid transaction format: " + toString(rlp) + " RLP: " + toHex(rlp.data()));
        throw;
    }
}

Address const& Transaction::safeSender() const noexcept
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

Address const& Transaction::sender() const
{
    if (!m_sender)
    {
        if (!m_vrs)
            BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

        auto p = recover(*m_vrs, sha3(WithoutSignature));
        if (!p)
            BOOST_THROW_EXCEPTION(InvalidSignature());
        m_sender = right160(dev::sha3(bytesConstRef(p.data(), sizeof(p))));
    }
    return m_sender;
}

SignatureStruct const& Transaction::signature() const
{
    if (!m_vrs)
        BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

    return *m_vrs;
}

/// encode the transaction to bytes
void Transaction::encode(bytes& _trans, IncludeSignature _sig) const
{
    RLPStream _s;
    if (m_type == NullTransaction)
        return;
    _s.appendList((_sig ? 3 : 0) + 7);
    _s << m_nonce << m_gasPrice << m_gas << m_blockLimit;
    if (m_type == MessageCall)
        _s << m_receiveAddress;
    else
        _s << "";
    _s << m_value << m_data;

    if (_sig)
    {
        if (!m_vrs)
            BOOST_THROW_EXCEPTION(TransactionIsUnsigned());

        // _s << (int)(m_vrs->v + VBase) << (u256)m_vrs->r << (u256)m_vrs->s;
        m_vrs->encode(_s);
    }

    _s.swapOut(_trans);
}

static const u256 c_secp256k1n(
    "115792089237316195423570985008687907852837564279074904382605163141518161494337");

void Transaction::checkLowS() const
{
    if (!m_vrs)
        BOOST_THROW_EXCEPTION(TransactionIsUnsigned());
    m_vrs->check();
}

int64_t Transaction::baseGasRequired(
    bool _contractCreation, bytesConstRef _data, EVMSchedule const& _es)
{
    int64_t g = _contractCreation ? _es.txCreateGas : _es.txGas;

    // Calculate the cost of input data.
    // No risk of overflow by using int64 until txDataNonZeroGas is quite small
    // (the value not in billions).
    for (auto i : _data)
        g += i ? _es.txDataNonZeroGas : _es.txDataZeroGas;
    return g;
}

h256 Transaction::sha3(IncludeSignature _sig) const
{
    if (_sig == WithSignature && m_hashWith)
        return m_hashWith;

    bytes s;
    encode(s, _sig);

    auto ret = dev::sha3(s);
    if (_sig == WithSignature)
        m_hashWith = ret;
    return ret;
}
