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
/** @file EVMInterface.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include "EVMHostContext.h"
#include <libethcore/Exceptions.h>
#include <memory>

namespace dev
{
namespace eth
{
class Result;
/// EVM Virtual Machine interface
class EVMInterface
{
public:
    EVMInterface() = default;
    virtual ~EVMInterface() = default;
    EVMInterface(EVMInterface const&) = delete;
    EVMInterface& operator=(EVMInterface const&) = delete;

    /// VM implementation
    virtual std::shared_ptr<Result> exec(executive::EVMHostContext& _ext, evmc_revision _rev,
        evmc_message* _msg, const uint8_t* _code, size_t _code_size) = 0;
};
}  // namespace eth
}  // namespace dev
