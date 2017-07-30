#include "SessionCAData.h"

using namespace dev::p2p;

void CABaseData::setSign(std::string sign)
{
	m_sign = sign;
}
std::string CABaseData::getSign()
{
	return m_sign;
}

void CABaseData::setSeed(std::string seed)
{
	m_seed = seed;
}

std::string CABaseData::getSeed()
{
	return m_seed;
}

std::string CABaseData::getPub256()
{
	return "";
}

void CABaseData::setPub256(std::string)
{

}

Signature CABaseData::getNodeSign()
{
	Signature signature;
	return signature;
}

void CABaseData::setNodeSign(const Signature&)
{

}
