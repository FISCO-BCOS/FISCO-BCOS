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
#pragma once

#include <evmjit.h>
#include <libevm/VMFace.h>

namespace dev
{
namespace eth
{

class JitVM: public VMFace
{
public:
	virtual bytesConstRef execImpl(u256& io_gas, ExtVMFace& _ext, OnOpFunc const& _onOp) override final;

	static evm_mode scheduleToMode(EVMSchedule const& _schedule);
	static bool isCodeReady(evm_mode _mode, h256 _codeHash);
	static void compile(evm_mode _mode, bytesConstRef _code, h256 _codeHash);

private:
	std::unique_ptr<VMFace> m_fallbackVM; ///< VM used in case of input data rejected by JIT
	bytes m_output;
};


}
}
