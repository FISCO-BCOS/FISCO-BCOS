/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file CpuHeavyPrecompiled.cpp
 * @author: jimmyshi
 * @date 2021-02-23
 */

#include "CpuHeavyPrecompiled.h"
#include "../../executive/BlockContext.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract HelloWorld {
    function get() public constant returns(string);
    function set(string _m) public;
}
*/

// set interface
const char *const METHOD_SORT = "sort(uint256,uint256)";

CpuHeavyPrecompiled::CpuHeavyPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl) {
    name2Selector[METHOD_SORT] = getFuncSelector(METHOD_SORT, _hashImpl);
}

std::string CpuHeavyPrecompiled::toString() {
    return "CpuHeavy";
}

void quickSort(std::vector<unsigned int> &arr, int left, int right) {
    int i = left;
    int j = right;
    if (i == j) return;
    unsigned int pivot = arr[left + (right - left) / 2];
    while (i <= j) {
        while (arr[i] < pivot) i++;
        while (pivot < arr[j]) j--;
        if (i <= j) {
            std::swap(arr[i], arr[j]);
            i++;
            j--;
        }
    }
    if (left < j)
        quickSort(arr, left, j);
    if (i < right)
        quickSort(arr, i, right);
}

void sort(int size) {
    std::vector<unsigned int> data = std::vector<unsigned int>(size);
    for (int x = 0; x < size; x++) {
        data[x] = size - x;
    }
    quickSort(data, 0, size - 1);
}


std::shared_ptr<PrecompiledExecResult> CpuHeavyPrecompiled::call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string &, const std::string &) {
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CpuHeavyPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_param));

    // parse function name
    //uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
            std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());

    u256 size, signature;
    codec->decode(data, size, signature);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    sort(size.convert_to<int>());

    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    callResult->setExecResult(bytes());
    return callResult;
}
