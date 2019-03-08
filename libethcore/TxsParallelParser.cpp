/*
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
 */
/**
 * @brief encode and decode transaction parallel
 *
 * @file TxsParallelParser.cpp
 * @author: jimmyshi
 * @date 2019-02-19
 */

#include "TxsParallelParser.h"
#include "Exceptions.h"

namespace dev
{
namespace eth
{
bytes TxsParallelParser::encode(Transactions& _txs)
{
    Offset_t txNum = _txs.size();
    if (txNum == 0)
        return bytes();

    bytes txBytes;
    std::vector<Offset_t> offsets(txNum + 1, 0);
    std::vector<bytes> txRLPs(txNum, bytes());

    // encode tx and caculate offset
#pragma omp parallel for
    for (Offset_t i = 0; i < txNum; ++i)
    {
        bytes txByte;
        _txs[i].encode(txByte);

        // record bytes size in offsets for caculating real offset
        offsets[i + 1] = txByte.size();

        // record bytes in txRLPs for caculating all transaction bytes
        txRLPs[i] = txByte;
    }

    // caculate real offset
    for (size_t i = 0; i < txNum; ++i)
    {
        offsets[i + 1] += offsets[i];
    }

    // encode according with this protocol
    bytes ret;
    ret += toBytes(Offset_t(txNum));
    for (size_t i = 0; i < offsets.size(); ++i)
        ret += toBytes(offsets[i]);

    for (size_t i = 0; i < txNum; ++i)
        ret += txRLPs[i];

    // std::cout << "tx encode:" << toHex(ret) << std::endl;
    return ret;
}

bytes TxsParallelParser::encode(std::vector<bytes> const& _txs)
{
    Offset_t txNum = _txs.size();
    if (txNum == 0)
        return bytes();

    bytes txBytes;
    std::vector<Offset_t> offsets(txNum + 1, 0);
    Offset_t offset = 0;

    // encode tx and caculate offset
    for (Offset_t i = 0; i < txNum; ++i)
    {
        bytes const& txByte = _txs[i];
        offsets[i] = offset;
        offset += txByte.size();
        txBytes += txByte;
    }
    offsets[txNum] = offset;  // write the end

    // encode according with this protocol
    bytes ret;
    ret += toBytes(Offset_t(txNum));
    for (size_t i = 0; i < offsets.size(); ++i)
        ret += toBytes(offsets[i]);

    ret += txBytes;

    // std::cout << "tx encode:" << toHex(ret) << std::endl;
    return ret;
}

// parallel decode transactions
void TxsParallelParser::decode(
    Transactions& _txs, bytesConstRef _bytes, CheckTransaction _checkSig, bool _withHash)
{
    try
    {
        if (_bytes.size() == 0)
            return;
        // std::cout << "tx decode:" << toHex(_bytes) << std::endl;
        Offset_t txNum = fromBytes(_bytes.cropped(0));
        vector_ref<Offset_t> offsets((Offset_t*)_bytes.cropped(sizeof(Offset_t)).data(), txNum + 1);
        _txs.resize(txNum);

        bytesConstRef txBytes = _bytes.cropped(sizeof(Offset_t) * (txNum + 2));

        if (offsets.size() == 0 || txBytes.size() == 0)
            throw;

#pragma omp parallel for schedule(dynamic, 125)
        for (Offset_t i = 0; i < txNum; ++i)
        {
            Offset_t offset = offsets[i];
            Offset_t size = offsets[i + 1] - offsets[i];

            _txs[i].decode(txBytes.cropped(offset, size), _checkSig);
            if (_withHash)
            {
                dev::h256 txHash = dev::sha3(txBytes.cropped(offset, size));
                _txs[i].updateTransactionHashWithSig(txHash);
            } /*
             LOG(DEBUG) << LOG_BADGE("DECODE") << LOG_DESC("decode tx:") << LOG_KV("i", i)
                        << LOG_KV("offset", offset)
                        << LOG_KV("code", toHex(txBytes.cropped(offset, size)));
                        */
        }
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(InvalidBlockFormat()
                              << errinfo_comment("Block transactions bytes is invalid")
                              << BadFieldError(1, _bytes.toString()));
    }
}

}  // namespace eth
}  // namespace dev