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
 * @file: SessionCAData.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcrypto/Common.h>
#include "Common.h"

using namespace dev;
namespace dev
{
	namespace p2p
	{

		class CABaseData
		{
		public:
			void setSign(std::string sign); 
			std::string getSign();
			void setSeed(std::string seed);
			std::string getSeed();

			virtual std::string getPub256();
			virtual void setPub256(std::string);

			virtual Signature getNodeSign();
			virtual void setNodeSign(const Signature&);
			virtual ~CABaseData(){};
		private:
			std::string m_seed;               //random data
			std::string m_sign;               //signed data of the random nonce

		};

	}

}
