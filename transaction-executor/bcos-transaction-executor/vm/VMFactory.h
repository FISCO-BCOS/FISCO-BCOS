/*
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
 * @brief factory of vm
 * @file VMFactory.h
 * @author: ancelmo
 * @date: 2022-12-15
 */

#pragma once
#include "VMInstance.h"
#include "bcos-utilities/Error.h"
#include <evmone/evmone.h>
#include <boost/throw_exception.hpp>
#include <memory>

namespace bcos::transaction_executor
{
enum class VMKind
{
    evmone,
};

// clang-format off
struct UnknownVMError : public bcos::Error {};
// clang-format on

class VMFactory
{
public:
    static VMInstance create(VMKind kind, bytesConstRef code, evmc_revision mode)
    {
        switch (kind)
        {
        case VMKind::evmone:
        {
            return VMInstance{
                std::make_shared<evmone::baseline::CodeAnalysis>(evmone::baseline::analyze(
                    mode, evmone::bytes_view((const uint8_t*)code.data(), code.size())))};
        }
        default:
            BOOST_THROW_EXCEPTION(UnknownVMError{});
        }
    }
};
}  // namespace bcos::transaction_executor