/*
    @CopyRight:
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @Legacy EVM context
 *
 * @file ExtVM.h
 * @author jimmyshi
 * @date 2018-09-21
 */

#pragma once

#include "StateFace.h"
#include <libethcore/Common.h>
#include <libevm/ExtVMFace.h>

namespace dev
{
namespace eth
{
/// Externality interface for the Virtual Machine providing access to world state.
class ExtVM
{
public:
    ExtVM(StateFace& _s) : m_s(_s) {}

    bytes const& codeAt(Address _a) { return m_s.code(_a); }
    void setCode(Address const& _address, bytes&& _code)
    {
        m_s.setCode(_address, std::move(_code));
    }

private:
    StateFace& m_s;
};

}  // namespace eth
}  // namespace dev
