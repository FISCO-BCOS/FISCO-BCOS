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
 * @file TxsParallelParser.h
 * @author: jimmyshi
 * @date 2019-02-19
 */
#pragma once
#include "Common.h"
#include "Transaction.h"
#include <libutilities/RLP.h>

namespace bcos
{
namespace eth
{
class TxsParallelParser
{
    /*
        To support decode transaction parallel in a block
        Encode/Decode Protocol:
        [txNum] [[0], [offset1], [offset2] ...] [[txByte0], [txByte1], [txByte2] ...]
                            |----------------------------------^
    */
    using Offset_t = uint32_t;

public:
    static bytes encode(std::shared_ptr<Transactions> _txs);
    static bytes encode(std::vector<bytes> const& _txs);
    static void decode(std::shared_ptr<Transactions> _txs, bytesConstRef _bytes,
        CheckTransaction _checkSig = CheckTransaction::Everything, bool _withHash = false);

private:
    static inline bytes toBytes(Offset_t _num)
    {
        bytes ret(sizeof(Offset_t), 0);
        *(Offset_t*)(ret.data()) = _num;
        return ret;
    }

    static inline Offset_t fromBytes(bytesConstRef _bs) { return Offset_t(*(Offset_t*)_bs.data()); }
};

}  // namespace eth
}  // namespace bcos
