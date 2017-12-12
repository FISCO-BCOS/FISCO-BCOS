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
 * @file: SessionWBCAData.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

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
