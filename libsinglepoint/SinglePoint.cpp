

#include "SinglePoint.h"

#include <libethereum/Interface.h>
#include <libethcore/ChainOperationParams.h>
#include <libethcore/CommonJS.h>
#include <libdevcore/easylog.h>
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
	SealEngineFace::populateFromParent(_bi, _parent);//自动调blockheader里面的

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
		SealEngineBase::generateSeal(bi, _data);//执行回调
		m_lasttimesubmit = chrono::high_resolution_clock::now();

		LOG(TRACE) << "SinglePoint::generateSeal Suc!!!!!!!" << bi.hash(WithoutSeal) << "#" << bi.number();
	}
}

bool SinglePoint::shouldSeal(Interface*)
{
	return true;
}


