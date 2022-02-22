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
 * @file ErrorConverter.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include "bcos-tars-protocol/tars/CommonProtocol.h"
#include <bcos-utilities/Error.h>

namespace bcostars
{
inline Error toTarsError(const bcos::Error& error)
{
    Error tarsError;
    tarsError.errorCode = error.errorCode();
    tarsError.errorMessage = error.errorMessage();

    return tarsError;
}

inline Error toTarsError(const bcos::Error::Ptr& error)
{
    Error tarsError;

    if (error)
    {
        tarsError.errorCode = error->errorCode();
        tarsError.errorMessage = error->errorMessage();
    }

    return tarsError;
}

inline bcos::Error::Ptr toBcosError(const bcostars::Error& error)
{
    if (error.errorCode == 0)
    {
        return nullptr;
    }

    auto bcosError = std::make_shared<bcos::Error>(error.errorCode, error.errorMessage);
    return bcosError;
}

inline bcos::Error::Ptr toBcosError(tars::Int32 ret)
{
    if (ret == 0)
    {
        return nullptr;
    }

    auto bcosError = std::make_shared<bcos::Error>(ret, "TARS error!");
    return bcosError;
}

}  // namespace bcostars