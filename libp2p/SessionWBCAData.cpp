#include "SessionWBCAData.h"

using namespace dev::p2p;

std::string WBCAData::getPub256()
{
	return m_pub256;
}

void WBCAData::setPub256(std::string pub256)
{
	m_pub256 = pub256;
}

Signature WBCAData::getNodeSign()
{
	return m_nodeSign;
}

void WBCAData::setNodeSign(const Signature& _nodeSign)
{
	m_nodeSign = _nodeSign;
}
