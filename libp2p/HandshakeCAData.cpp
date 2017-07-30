#include "HandshakeCAData.h"

using namespace dev;
using namespace dev::p2p;

void RLPBaseData::setSeed(bytes seed)
{
	seed.swap(m_seed);
}

bytes & RLPBaseData::getSeed()
{
	return m_seed;
}
