
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
			std::string m_seed;               //随机数
			std::string m_sign;               //对CA随机数信息的sign

		};

	}

}
