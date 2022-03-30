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
 * @file VMFactory.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */


#include "VMFactory.h"
#include "VMInstance.h"
#include <BCOS_WASM.h>
#include <evmc/loader.h>
#include <evmone/evmone.h>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace bcos
{
namespace executor
{
namespace
{
auto g_kind = VMKind::evmone;

/// The pointer to VMInstance create function in DLL VMInstance VM.
///
/// This variable is only written once when processing command line arguments,
/// so access is thread-safe.
evmc_create_fn g_evmcCreateFn;

/// A helper type to build the tabled of VM implementations.
///
/// More readable than std::tuple.
/// Fields are not initialized to allow usage of construction with initializer lists {}.
struct VMKindTableEntry
{
    VMKind kind;
    const char* name;
};

/// The table of available VM implementations.

#if 0
VMKindTableEntry vmKindsTable[] = {{VMKind::BcosWasm, "bcos wasm"}, {VMKind::evmone, "evmone"}};
void setVMKind(const std::string& _name)
{
    for (auto& entry : vmKindsTable)
    {
        // Try to find a match in the table of VMs.
        if (_name == entry.name)
        {
            g_kind = entry.kind;
            return;
        }
    }
    // If not match for predefined VM names, try loading it as an VMInstance DLL.
    evmc_loader_error_code ec;
    g_evmcCreateFn = evmc_load(_name.c_str(), &ec);
    switch (ec)
    {
    case EVMC_LOADER_SUCCESS:
        break;
    case EVMC_LOADER_CANNOT_OPEN:
        BOOST_THROW_EXCEPTION(
            po::validation_error(po::validation_error::invalid_option_value, "vm", _name, 1));
    case EVMC_LOADER_SYMBOL_NOT_FOUND:
        BOOST_THROW_EXCEPTION(std::system_error(std::make_error_code(std::errc::invalid_seek),
            "loading " + _name + " failed: VMInstance create function not found"));
    default:
        BOOST_THROW_EXCEPTION(
            std::system_error(std::error_code(static_cast<int>(ec), std::generic_category()),
                "loading " + _name + " failed"));
    }
    g_kind = VMKind::DLL;
}
#endif
}  // namespace

VMInstance VMFactory::create()
{
    return create(g_kind);
}

VMInstance VMFactory::create(VMKind _kind)
{
    switch (_kind)
    {
    case VMKind::BcosWasm:
        return VMInstance{evmc_create_bcoswasm()};
    case VMKind::evmone:
        return VMInstance{evmc_create_evmone()};
    case VMKind::DLL:
        return VMInstance{g_evmcCreateFn()};
    default:
        return VMInstance{evmc_create_evmone()};
    }
}
}  // namespace executor
}  // namespace bcos
