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
/** @file ChainOperationsParams.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2015
 */

#pragma once

#include <libdevcore/Common.h>
#include "Common.h"
#include <libevmcore/EVMSchedule.h>
#include <vector>

namespace dev
{
namespace eth
{

class PrecompiledContract
{
public:
	PrecompiledContract() = default;
	PrecompiledContract(std::function<bigint(unsigned)> const& _cost, std::function<void(bytesConstRef, bytesRef)> const& _exec):
		m_cost(_cost),
		m_execute(_exec)
	{}
	PrecompiledContract(unsigned _base, unsigned _word, std::function<void(bytesConstRef, bytesRef)> const& _exec);

	bigint cost(bytesConstRef _in) const { return m_cost(_in.size()); }
	void execute(bytesConstRef _in, bytesRef _out) const { m_execute(_in, _out); }

private:
	std::function<bigint(unsigned)> m_cost;
	std::function<void(bytesConstRef, bytesRef)> m_execute;
};

struct ChainOperationParams
{
	ChainOperationParams();

	explicit operator bool() const { return accountStartNonce != Invalid256; }

	/// The chain sealer name: e.g. Ethash, NoProof, BasicAuthority
	std::string sealEngineName = "NoProof";

	/// General chain params.
	u256 blockReward = 0;
	u256 maximumExtraDataSize = 1024;
	u256 accountStartNonce = 0;
	bool tieBreakingGas = true;

	/// Precompiled contracts as specified in the chain params.
	std::unordered_map<Address, PrecompiledContract> precompiled;

	/**
	 * @brief Additional parameters.
	 *
	 * e.g. Ethash specific:
	 * - minGasLimit
	 * - maxGasLimit
	 * - gasLimitBoundDivisor
	 * - minimumDifficulty
	 * - difficultyBoundDivisor
	 * - durationLimit
	 */
	std::unordered_map<std::string, std::string> otherParams;

	/// Convenience method to get an otherParam as a u256 int.
	u256 u256Param(std::string const& _name) const;
	std::string param(std::string const& _name) const;

	bool evmEventLog = false; //是否打印event log
	bool evmCoverLog = false; //是否打印覆盖率统计日志
	Address sysytemProxyAddress;//系统代理合约地址
	Address god;//上帝帐号

	std::string listenIp;
	int rpcPort = 6789;
	int p2pPort = 16789;
	std::string wallet;
	std::string keystoreDir;
	std::string dataDir;
	std::string logFileConf;

	//dfs related parameters
	string nodeId;
	string groupId;
	string storagePath;
	
	std::string vmKind;
	unsigned networkId;
	int logVerbosity = 4;

	u256 intervalBlockTime = 3000;

	bool broadcastToNormalNode = false; // 是否在PBFT共识阶段广播信息给非记账者

	u256 godMinerStart=0;
	u256 godMinerEnd=0;
	std::map<std::string, NodeConnParams> godMinerList;

};

}
}
