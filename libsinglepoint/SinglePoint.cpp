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
/**
 * @file: SinglePoint.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "SinglePoint.h"

#include <libethereum/Interface.h>
#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>

using namespace std;
using namespace dev;
using namespace eth;



void SinglePoint::init()
{
	ETH_REGISTER_SEAL_ENGINE(SinglePoint);
}

SinglePoint::SinglePoint()
{

}

strings SinglePoint::sealers() const
{
	return {"cpu"};
}





void SinglePoint::verify(Strictness , BlockHeader const& , BlockHeader const& , bytesConstRef ) const
{

}

void SinglePoint::verifyTransaction(ImportRequirements::value , TransactionBase const& , BlockHeader const& ) const
{

}

u256 SinglePoint::childGasLimit(BlockHeader const& , u256 const& ) const
{
	return 0;
}



u256 SinglePoint::calculateDifficulty(BlockHeader const& , BlockHeader const& ) const
{
	return 1;
}

void SinglePoint::populateFromParent(BlockHeader& _bi, BlockHeader const& _parent) const
{
	SealEngineFace::populateFromParent(_bi, _parent);// call parent function 自动调blockheader里面的

}

bool SinglePoint::quickVerifySeal(BlockHeader const& ) const
{
	return true;
}

bool SinglePoint::verifySeal(BlockHeader const& ) const
{
	return true;
}
void SinglePoint::setIntervalBlockTime(u256 _intervalBlockTime)
{
	if ( _intervalBlockTime < 1000 )
		_intervalBlockTime = 1000;

	m_intervalBlockTime = _intervalBlockTime;
}

void SinglePoint::generateSeal(BlockHeader const& bi, bytes const& _data)
{
	auto  d = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - m_lasttimesubmit);

	if ( d.count() >= getIntervalBlockTime() )
	{
		SealEngineBase::generateSeal(bi, _data);// exec callback 执行回调
		m_lasttimesubmit = chrono::high_resolution_clock::now();

		LOG(INFO) << "SinglePoint::generateSeal Suc!!!!!!!" << bi.hash(WithoutSeal) << "#" << bi.number();
	}


}

bool SinglePoint::shouldSeal(Interface*)
{
	return true;
}


