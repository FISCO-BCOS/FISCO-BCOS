#pragma once

#include "SessionCAData.h"

namespace dev {

	namespace p2p {
		class WBCAData : public CABaseData
		{
		public:
			virtual std::string getPub256(); 
			virtual void setPub256(std::string pub256);
			Signature getNodeSign(); 
			void setNodeSign(const Signature& _nodeSign); 
		private:
			std::string m_pub256;             //CA证书内容的sha256
			Signature m_nodeSign;           //对节点信息的sign
		};
	}
}
