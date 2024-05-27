/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file TransactionBuilderV2.cpp
 * @author: kyonGuo
 * @date 2024/3/5
 */

#include "TransactionBuilderV2.h"

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcostars;
using namespace bcos::cppsdk::utilities;
using namespace bcos::crypto;


bcos::bytes TransactionBuilderV2::createSignedTransactionWithSign(bcos::bytes _signData,
    crypto::HashType _hash, bcostars::TransactionData const& _txData, int32_t _attribute,
    std::string _extraData, bool checkHash, CryptoType _cryptoType)
{
    bcostars::Transaction tx;
    tx.data = _txData;
    tx.signature.insert(tx.signature.begin(), _signData.begin(), _signData.end());
    if (checkHash)
    {
        auto hash = TransactionBuilder::calculateTransactionDataHash(_cryptoType, _txData);
        if (hash != _hash)
        {
            throw std::invalid_argument(
                "The hash of the transaction data is not equal to the given hash");
        }
    }
    tx.dataHash.insert(tx.dataHash.begin(), _hash.begin(), _hash.end());
    tx.attribute = _attribute;
    tx.extraData = std::move(_extraData);

    tars::TarsOutputStream<tars::BufferWriter> output;
    tx.writeTo(output);
    bcos::bytes buffer;
    buffer.assign(output.getBuffer(), output.getBuffer() + output.getLength());
    return buffer;
}


std::pair<bcos::bytes, bcos::bytes> TransactionBuilderV2::createSignedTransaction(
    bcos::crypto::KeyPairInterface const& _keyPair, bcostars::TransactionData const& _txData,
    int32_t _attribute, std::string _extraData)
{
    auto txDataHash = calculateTransactionDataHash(_keyPair.keyPairType(), _txData);
    auto signData = TransactionBuilder::signTransactionDataHash(_keyPair, txDataHash);
    auto tx = createTransaction(_txData, *signData, txDataHash, _attribute, _extraData);
    tars::TarsOutputStream<tars::BufferWriter> output;
    tx->writeTo(output);
    bcos::bytes txBytes;
    txBytes.assign(output.getBuffer(), output.getBuffer() + output.getLength());

    return {txDataHash.asBytes(), txBytes};
}
