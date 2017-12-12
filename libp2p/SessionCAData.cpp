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
 * @file: SessionCAData.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

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
