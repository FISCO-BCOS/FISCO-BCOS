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
 * @file JsonDataConvertUtility.h
 */

#include "JsonDataConvertUtility.h"

using namespace std;

namespace bcos
{
bytes jonStringToBytes(string const& _stringData, OnFailed _action)
{
    try
    {
        return *fromHexString(_stringData);
    }
    catch (...)
    {
        if (_action == OnFailed::InterpretRaw)
        {
            return asBytes(_stringData);
        }
        else if (_action == OnFailed::Throw)
        {
            throw invalid_argument("Cannot intepret '" + _stringData +
                                   "' as bytes; must be 0x-prefixed hex or decimal.");
        }
    }
    return bytes();
}
}  // namespace bcos
