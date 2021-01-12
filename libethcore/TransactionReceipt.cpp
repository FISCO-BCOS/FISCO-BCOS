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
/** @file TransactionReceipt.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "TransactionReceipt.h"
#include "Exceptions.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

TransactionReceipt::TransactionReceipt(bytesConstRef _rlp)
{
    RLP r(_rlp);
    decode(r);
}

TransactionReceipt::TransactionReceipt(h256 const& _root, u256 const& _gasUsed,
    LogEntries const& _log, eth::TransactionException _status, bytes _bytes,
    Address const& _contractAddress)
  : m_status(_status),
    m_stateRoot(_root),
    m_gasUsed(_gasUsed),
    m_contractAddress(_contractAddress),
    m_bloom(eth::bloom(_log)),
    m_outputBytes(_bytes),
    m_log(_log)
{}

TransactionReceipt::TransactionReceipt(TransactionReceipt const& _other)
  : m_status(_other.status()),
    m_stateRoot(_other.stateRoot()),
    m_gasUsed(_other.gasUsed()),
    m_remainGas(_other.remainGas()),
    m_contractAddress(_other.contractAddress()),
    m_bloom(_other.bloom()),
    m_outputBytes(_other.outputBytes()),
    m_log(_other.log())
{}

void TransactionReceipt::streamRLP(RLPStream& _s) const
{
    if (g_BCOSConfig.version() >= V2_8_0)
    {
        _s.appendList(8) << m_stateRoot << m_gasUsed << m_contractAddress << m_bloom
                         << static_cast<u256>(m_status) << m_outputBytes << m_remainGas;
    }
    else
    {
        _s.appendList(7) << m_stateRoot << m_gasUsed << m_contractAddress << m_bloom
                         << static_cast<u256>(m_status) << m_outputBytes;
    }
    _s.appendList(m_log.size());
    for (LogEntry const& l : m_log)
        l.streamRLP(_s);
}

void TransactionReceipt::decode(bytesConstRef receiptsBytes)
{
    RLP const rlp(receiptsBytes);
    decode(rlp);
}

void TransactionReceipt::decodeLog(size_t _fromIndex, RLP const& _r)
{
    for (auto it = _r[_fromIndex].begin(); it != _r[_fromIndex].end(); it++)
    {
        m_log.emplace_back(*it);
    }
}

void TransactionReceipt::decode(RLP const& r)
{
    try
    {
        if (!r.isList())
            BOOST_THROW_EXCEPTION(InvalidTransactionFormat()
                                  << errinfo_comment("TransactionReceipt RLP must be a list"));
        m_stateRoot = (h256)r[0];
        m_gasUsed = (u256)r[1];
        m_contractAddress = (Address)r[2];
        m_bloom = (LogBloom)r[3];
        m_status = static_cast<eth::TransactionException>((uint32_t)r[4]);
        if (r[5].isData())
        {
            m_outputBytes = r[5].toBytes();
        }
        if (g_BCOSConfig.version() >= V2_8_0)
        {
            m_remainGas = (u256)r[6];
            decodeLog(7, r);
        }
        else
        {
            decodeLog(6, r);
        }
    }
    catch (Exception& _e)
    {
        _e << errinfo_name(
            "invalid transaction format: " + toString(r) + " RLP: " + toHex(r.data()));
        throw;
    }
}

std::ostream& dev::eth::operator<<(std::ostream& _out, TransactionReceipt const& _r)
{
    _out << "Root: " << _r.stateRoot() << "\n";
    _out << "Gas used: " << _r.gasUsed() << "\n";
    _out << "contractAddress : " << _r.contractAddress() << "\n";
    _out << "status : " << int(_r.status()) << "\n";
    _out << "output: " << toHex(_r.outputBytes()) << " \n";
    _out << "Logs: " << _r.log().size() << " entries:"
         << "\n";
    for (LogEntry const& i : _r.log())
    {
        _out << "Address " << i.address << ". Topics:"
             << "\n";
        for (auto const& j : i.topics)
            _out << "  " << j << "\n";
        _out << "  Data: " << toHex(i.data) << "\n";
    }
    _out << "Bloom: " << _r.bloom() << "\n";
    return _out;
}
