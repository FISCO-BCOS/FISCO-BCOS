/*
 *  Copyright (C) 2020 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
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
namespace protocol
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

}  // namespace protocol
}  // namespace bcos
