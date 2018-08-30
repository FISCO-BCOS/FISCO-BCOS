#pragma once

#include "RPCInitializer.h"
#include "CommonInitializer.h"
#include "SecureInitializer.h"
#include "AMDBInitializer.h"
#include "PBFTInitializer.h"
#include "P2PInitializer.h"
#include "ConsoleInitializer.h"

namespace dev {

namespace eth {

//相当于spring的applicationContext
class Initializer: public std::enable_shared_from_this<Initializer> {
public:
	typedef std::shared_ptr<Initializer> Ptr;

	void init(const std::string &path);
	//void initByPath(const std::string)

	RPCInitializer::Ptr rpcInitializer();
	CommonInitializer::Ptr commonInitializer();
	SecureInitializer::Ptr secureInitializer();
	AMDBInitializer::Ptr amdbInitializer();
	PBFTInitializer::Ptr pbftInitializer();
	p2p::P2PInitializer::Ptr p2pInitializer();
	ConsoleInitializer::Ptr consoleInitializer();

private:

	RPCInitializer::Ptr _rpcInitializer;
	CommonInitializer::Ptr _commonInitializer;
	SecureInitializer::Ptr _secureInitializer;
	AMDBInitializer::Ptr _amdbInitializer;
	PBFTInitializer::Ptr _pbftInitializer;
	p2p::P2PInitializer::Ptr _p2pInitializer;
	ConsoleInitializer::Ptr _consoleInitializer;

};

}

}
