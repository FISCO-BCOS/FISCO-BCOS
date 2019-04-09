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
/** @file VMFace.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include "ExtVMFace.h"
#include <libethcore/Exceptions.h>
#include <memory>

namespace dev
{
namespace eth
{
/// EVM Virtual Machine interface
class VMFace
{
public:
    VMFace() = default;
    virtual ~VMFace() = default;
    VMFace(VMFace const&) = delete;
    VMFace& operator=(VMFace const&) = delete;

    /// VM implementation
    virtual owning_bytes_ref exec(u256& io_gas, ExtVMFace& _ext, OnOpFunc const& _onOp) = 0;
};
}  // namespace eth
}  // namespace dev
