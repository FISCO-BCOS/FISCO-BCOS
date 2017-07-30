
#pragma once
#include <libdevcore/Common.h>
#include "Common.h"

namespace dev {
	namespace p2p {
		class RLPBaseData {
		public:
			void setSeed(bytes seed);

			bytes &getSeed();

		private:
			bytes m_seed;
		};
	}
}
