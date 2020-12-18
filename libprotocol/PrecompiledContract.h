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
/** @file PrecompiledContract.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2015
 */

#pragma once

#include <libprotocol/Precompiled.h>
#include <libutilities/Common.h>

namespace bcos
{
namespace protocol
{
class PrecompiledContract
{
public:
    PrecompiledContract() = default;
    PrecompiledContract(PrecompiledPricer const& _cost, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0)
      : m_cost(_cost), m_execute(_exec), m_startingBlock(_startingBlock)
    {}
    PrecompiledContract(unsigned _base, unsigned _word, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0);

    bigint cost(bytesConstRef _in) const { return m_cost(_in); }
    std::pair<bool, bytes> execute(bytesConstRef _in) const { return m_execute(_in); }

    u256 const& startingBlock() const { return m_startingBlock; }

private:
    PrecompiledPricer m_cost;
    PrecompiledExecutor m_execute;
    u256 m_startingBlock = 0;
};

}  // namespace protocol
}  // namespace bcos
