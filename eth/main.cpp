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
 * @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Tasha Carl <tasha@carl.pro> - I here by place all my contributions in this file under MIT licence, as specified by http://opensource.org/licenses/MIT.
 * @date 2014
 * Ethereum client.

 */

#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <clocale>
#include <signal.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/filesystem.hpp>

#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>

#include <libevm/VM.h>
#include <libevm/VMFactory.h>
#include <libethcore/KeyManager.h>
#include <libethcore/ICAP.h>
#include <libethereum/All.h>
#include <libethereum/BlockChainSync.h>
#include <libpbftseal/PBFT.h>
#include <libsinglepoint/SinglePointClient.h>
#include <libsinglepoint/SinglePoint.h>
#include <libraftseal/Raft.h>
#include <libwebthree/WebThree.h>

#include <libweb3jsonrpc/AccountHolder.h>
#include <libweb3jsonrpc/Eth.h>
#include <libweb3jsonrpc/SafeHttpServer.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#include <libwebthree/SystemManager.h>
#include <libweb3jsonrpc/ModularServer.h>
#include <libweb3jsonrpc/IpcServer.h>
#include <libweb3jsonrpc/LevelDB.h>
#include <libweb3jsonrpc/Net.h>
#include <libweb3jsonrpc/Web3.h>
#include <libweb3jsonrpc/SessionManager.h>
#include <libweb3jsonrpc/AdminNet.h>
#include <libweb3jsonrpc/AdminEth.h>
#include <libweb3jsonrpc/AdminUtils.h>
#include <libweb3jsonrpc/Personal.h>
#include <libweb3jsonrpc/Debug.h>
#include <libweb3jsonrpc/Test.h>
#include "Farm.h"

//#include <ethminer/MinerAux.h>
#include "AccountManager.h"

#include <libdiskencryption/CryptoParam.h>
#include <libdiskencryption/GenKey.h>
#include <libdiskencryption/EncryptFile.h>
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libweb3jsonrpc/RPCallback.h>
#if ETH_ENCRYPTTYPE
#include <libdevcrypto/sm2/sm2.h>
#endif
#include <libdevcrypto/Common.h>
#include <libinitializer/Initializer.h>

INITIALIZE_EASYLOGGINGPP

using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
using namespace boost::algorithm;

static std::atomic<bool> g_silence = {false};

#include "genesisInfoForWe.h"
#include "GenesisInfo.h"

void help()
{
	cout
	        << "Usage eth [OPTIONS]" << "\n"
	        << "Options:" << "\n" << "\n";
	cout
	        << "    --config <file>  Specify the config file." << "\n"
	        << "    --genesis <file> Specify the genesis file." << "\n"
	        << "\n"
	        << "General Options:" << "\n"
	        << "    -V,--version  Show the version and exit." << "\n"
	        << "    -h,--help  Show this help message and exit." << "\n"
	        << "\n"
	        ;
	exit(0);
}

string ethCredits(bool _interactive = false)
{
	std::ostringstream cout;
	cout
		<< "FISCO-BCOS " << dev::Version << "\n"
		<< dev::Copyright << "\n"
		<< "  See the README for contributors and credits."
		<< "\n";

	if (_interactive)
		cout << "Type 'exit' to quit"
			 << "\n"
			 << "\n";
	return cout.str();
}

void version()
{
#if ETH_ENCRYPTTYPE
        cout << "FISCO-BCOS version " << dev::Version <<"-gm" << "\n";
#else
        cout << "FISCO-BCOS version " << dev::Version << "\n";
#endif
	cout << "FISCO-BCOS network protocol version: " << dev::eth::c_protocolVersion << "\n";
	cout << "Client database version: " << dev::eth::c_databaseVersion << "\n";
	cout << "Build: " << DEV_QUOTED(ETH_BUILD_PLATFORM) << "/" << DEV_QUOTED(ETH_BUILD_TYPE) << "\n";
	exit(0);
}

/*
The equivalent of setlocale(LC_ALL, “C”) is called before any user code is run.
If the user has an invalid environment setting then it is possible for the call
to set locale to fail, so there are only two possible actions, the first is to
throw a runtime exception and cause the program to quit (default behaviour),
or the second is to modify the environment to something sensible (least
surprising behaviour).

The follow code produces the least surprising behaviour. It will use the user
specified default locale if it is valid, and if not then it will modify the
environment the process is running in to use a sensible default. This also means
that users do not need to install language packs for their OS.
*/
void setDefaultOrCLocale()
{
#if __unix__
	if (!std::setlocale(LC_ALL, ""))
	{
		setenv("LC_ALL", "C", 1);
	}
#endif
}

void importPresale(KeyManager& _km, string const& _file, function<string()> _pass)
{
	KeyPair k = _km.presaleSecret(contentsString(_file), [&](bool) { return _pass(); });
	_km.import(k.secret(), "Presale wallet" + _file + " (insecure)");
}

Address c_config = Address("ccdeac59d35627b7de09332e819d5159e7bb7250");
string pretty(h160 _a, dev::eth::State const& _st)
{
	string ns;
	h256 n;
	if (h160 nameReg = (u160)_st.storage(c_config, 0))
		n = _st.storage(nameReg, (u160)(_a));
	if (n)
	{
		std::string s((char const*)n.data(), 32);
		if (s.find_first_of('\0') != string::npos)
			s.resize(s.find_first_of('\0'));
		ns = " " + s;
	}
	return ns;
}

inline bool isPrime(unsigned _number)
{
	if (((!(_number & 1)) && _number != 2 ) || (_number < 2) || (_number % 3 == 0 && _number != 3))
		return false;
	for (unsigned k = 1; 36 * k * k - 12 * k < _number; ++k)
		if ((_number % (6 * k + 1) == 0) || (_number % (6 * k - 1) == 0))
			return false;
	return true;
}

enum class NodeMode
{
	PeerServer,
	Full
};

enum class OperationMode
{
	Node,
	Import,
	Export,
	ExportGenesis
};

enum class Format
{
	Binary,
	Hex,
	Human
};

class ExitHandler: public SystemManager
{
public:
	void exit() { exitHandler(0); }
	static void exitHandler(int) { s_shouldExit = true; }
	bool shouldExit() const { return s_shouldExit; }

private:
	static bool s_shouldExit;
};

bool ExitHandler::s_shouldExit = false;

static map<string, unsigned int> s_mlogIndex;

void rolloutHandler(const char* filename, std::size_t )
{
	std::stringstream stream;

	map<string, unsigned int>::iterator iter = s_mlogIndex.find(filename);
	if (iter != s_mlogIndex.end())
	{
		stream << filename << "." << iter->second++;
		s_mlogIndex[filename] = iter->second++;
	}
	else
	{
		stream << filename << "." << 0;
		s_mlogIndex[filename] = 0;
	}
	boost::filesystem::rename(filename, stream.str().c_str());
}

void logRotateByTime()
{	
	const std::chrono::seconds wakeUpDelta = std::chrono::seconds(20);
	static auto nextWakeUp = std::chrono::system_clock::now();
	
	if(std::chrono::system_clock::now() > nextWakeUp)
	{
		nextWakeUp = std::chrono::system_clock::now() + wakeUpDelta;
	}
	else
	{
		return;
	}

	auto L = el::Loggers::getLogger("default");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called default!";
	}
	else
	{
		L->reconfigure();	
	}

	L = el::Loggers::getLogger("fileLogger");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called fileLogger!";
	}
	else
	{
		L->reconfigure();	
	} 

	L = el::Loggers::getLogger("statLogger");
	if(L == nullptr)
	{
		LOG(ERROR)<<"Oops, it is not called statLogger!";
	}
	else
	{
		L->reconfigure();	
	} 
 
}
//日志配置文件放到log目录
void initEasylogging(const std::string logConfig)
{
	string logconf = logConfig;

	el::Loggers::addFlag(el::LoggingFlag::MultiLoggerSupport); // Enables support for multiple loggers
	el::Loggers::addFlag(el::LoggingFlag::StrictLogFileSizeCheck);
	el::Loggers::setVerboseLevel(10);
	if (el::base::utils::File::pathExists(logconf.c_str(), true))
	{
		el::Logger* fileLogger = el::Loggers::getLogger("fileLogger"); // Register new logger
		el::Configurations conf(logconf.c_str());
		el::Configurations allConf;
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToFile));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::ToStandardOutput));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Format));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::Filename));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::SubsecondPrecision));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MillisecondsWidth));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::PerformanceTracking));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::MaxLogFileSize));
		allConf.set(conf.get(el::Level::Global, el::ConfigurationType::LogFlushThreshold));
		allConf.set(conf.get(el::Level::Trace, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Debug, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Fatal, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Error, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Warning, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Verbose, el::ConfigurationType::Enabled));
		allConf.set(conf.get(el::Level::Info, el::ConfigurationType::Enabled));
		el::Loggers::reconfigureLogger("default", allConf);
		el::Loggers::reconfigureLogger(fileLogger, conf);

		// stat log，依托于 default 的配置
		el::Logger* statLogger = el::Loggers::getLogger("statLogger");
		el::Configurations statConf = allConf;
		el::Configuration* fileConf = conf.get(el::Level::Global, el::ConfigurationType::Filename);
		
		string stat_prefix = "stat_";
		string stat_path = fileConf->value();
		if (!stat_path.empty()) 
		{
			size_t pos = stat_path.find_last_of("/"); // not support for windows
			if ((int)pos == -1) // log file just has name not path
				stat_path = stat_prefix + stat_path;
			else
				stat_path.insert(pos + 1, stat_prefix);
		} 
		else 
		{
			stat_path = stat_prefix + "log_%datetime{%Y%M%d%H}.log";
		}

		el::Loggers::reconfigureLogger("statLogger", statConf);
	}
	el::Helpers::installPreRollOutCallback(rolloutHandler);
}

bool isTrue(std::string const& _m)
{
	return _m == "on" || _m == "yes" || _m == "true" || _m == "1";
}

bool isFalse(std::string const& _m)
{
	return _m == "off" || _m == "no" || _m == "false" || _m == "0";
}


int main(int argc, char** argv)
{
	setDefaultOrCLocale();
	Defaults::get();
	NoProof::init();
	PBFT::init();
	Raft::init();
	SinglePoint::init();

	/// General params for Node operation
	NodeMode nodeMode = NodeMode::Full;
	/// bool interactive = false;

	ChainParams chainParams(genesisInfo(eth::Network::MainNetwork), genesisStateRoot(eth::Network::MainNetwork));

	WithExisting withExisting = WithExisting::Trust;

	std::string configJSON;
	std::string genesisJSON;

	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];
		if ((arg == "--genesis-json" || arg == "--genesis") && i + 1 < argc)
		{
			try
			{
				genesisJSON = contentsString(argv[++i]);
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "--config" && i + 1 < argc)
		{
			try
			{
				setConfigPath(argv[++i]);
				configJSON = contentsString(getConfigPath());
			}
			catch (...)
			{
				LOG(ERROR) << "Bad " << arg << " option: " << argv[i] << "\n";
				return -1;
			}
		}
		else if (arg == "-h" || arg == "--help")
			help();
		else if (arg == "-V" || arg == "--version")
			version();
		else
		{
			LOG(ERROR) << "Invalid argument: " << arg << "\n";
			exit(-1);
		}
	}

	if (!genesisJSON.empty())
	{
		try
		{
			chainParams = chainParams.loadGenesis(genesisJSON);
		}
		catch (...)
		{
			LOG(ERROR) << "provided genesis block description is not well formatted" << "\n";
			LOG(ERROR) << "sample: " << "\n" << c_genesisJsonForWe << "\n";
			return 0;
		}
	}
	else
	{
		LOG(ERROR) << "Provide genesis block file: --genesis xxx" << "\n";
		LOG(ERROR) << "sample: " << "\n" << c_genesisJsonForWe << "\n";
		return 0;
	}

	cout << EthGrayBold "---------------FISCO-BCOS-PRO, a C++ BlockChain client--------------" EthReset << "\n";
	VMFactory::setKind(VMKind::Interpreter);
	cout << EthGrayBold "---------------------------------------------------------------" EthReset << "\n";

	std::string secretsPath = SecretStore::defaultPath();

	auto caps = set<string> {"eth"};

	//启动channelServer
	Initializer::Ptr initializer = std::make_shared<Initializer>();
	initializer->init(getConfigPath());
	chainParams.setInitializer(initializer);

	initEasylogging(initializer->commonInitializer()->logConfig());

	setDataDir(initializer->commonInitializer()->dataPath());

		//添加落盘加密代码
	bytes nodesState;
	if (chainParams.cryptoMod != 0)
	{
		GenKey genKey;
		CryptoParam cryptoParam;
		string _filePath = getDataDir() + "/cryptomod.json";
		if(boost::filesystem::exists(_filePath) && boost::filesystem::is_regular_file(_filePath))
		{
			cryptoParam = cryptoParam.loadDataCryptoConfig(_filePath);
			int cryptoMod = cryptoParam.m_cryptoMod;
			string superKey = cryptoParam.m_superKey;
			string kcUrl = cryptoParam.m_kcUrl;
			LOG(INFO) << "cryptoMod:" << cryptoMod<<" kcUrl:"<<kcUrl;
			
			EncryptFile _encryptFile(cryptoMod,superKey,kcUrl);
			string dataKey = _encryptFile.getDataKey();
			if (dataKey == "")
			{
				LOG(ERROR)<<"datakey.encrypt data is null";
				exit(-1);
			}
			dev::setDataKey(dataKey.substr(0,8),dataKey.substr(8,8),dataKey.substr(16,8),dataKey.substr(24,8));
			dev::setCryptoMod(cryptoMod);
		}
		else
		{
			LOG(ERROR)<<_filePath + " is not exist";
			exit(-1);
		}
	}
	else
	{
		dev::setCryptoMod(chainParams.cryptoMod);
		nodesState = contents(getDataDir() + "/network.rlp");
	}
	dev::WebThreeDirect web3(
	    WebThreeDirect::composeClientVersion("eth"),
	    getDataDir(),
	    chainParams,
	    withExisting,
	    nodeMode == NodeMode::Full ? caps : set<string>(),
	    false
	);

	cout << "NodeID=" << toString(web3.id()) << "\n";
	cout << ethCredits();

	web3.startNetwork();
	cout << "Node ID: " << web3.enode() << "\n";
	unique_ptr<rpc::SessionManager> sessionManager;

	using FullServer = ModularServer <
							   rpc::EthFace, rpc::DBFace,
							   rpc::NetFace, rpc::Web3Face,
							   rpc::AdminNetFace, rpc::DebugFace>;
	sessionManager.reset(new rpc::SessionManager());

	auto ethFace = new rpc::Eth(*web3.ethereum());

	ModularServer<> *channelModularServer = NULL;
	if(chainParams.getInitializer()->rpcInitializer()->channelRPCServer()) {
		channelModularServer =
			new FullServer(ethFace, new rpc::LevelDB(),
				new rpc::Net(web3),
				new rpc::Web3(web3.clientVersion()),
				new rpc::AdminNet(web3, *sessionManager.get()),
				new rpc::Debug(*web3.ethereum()));

		channelModularServer->addConnector(
				chainParams.getInitializer()->rpcInitializer()->channelRPCServer().get());

		channelModularServer->StartListening();

		cout << "ChannelServer started." << std::endl;
	}

	ModularServer<> *jsonrpcHttpServer = NULL;
	if(chainParams.getInitializer()->rpcInitializer()->safeHttpServer()) {
		jsonrpcHttpServer =
			new FullServer(ethFace, new rpc::LevelDB(),
				new rpc::Net(web3),
				new rpc::Web3(web3.clientVersion()),
				new rpc::AdminNet(web3, *sessionManager.get()),
				new rpc::Debug(*web3.ethereum()));

		jsonrpcHttpServer->addConnector(chainParams.getInitializer()->rpcInitializer()->safeHttpServer().get());

		jsonrpcHttpServer->StartListening();

		cout << "JsonrpcHttpServer started." << std::endl;
	}

	if(chainParams.getInitializer()->consoleInitializer()->consoleServer()) {
		chainParams.getInitializer()->consoleInitializer()->consoleServer()->setInterface(web3.ethereum());
		chainParams.getInitializer()->consoleInitializer()->consoleServer()->StartListening();

		cout << "ConsoleServer started." << std::endl;
	}

	LOG(INFO) << "Start sealing";
	web3.ethereum()->startSealing();

	ExitHandler exitHandler;
	signal(SIGABRT, &ExitHandler::exitHandler);
	signal(SIGTERM, &ExitHandler::exitHandler);
	signal(SIGINT, &ExitHandler::exitHandler);

	while (!exitHandler.shouldExit()) {
		this_thread::sleep_for(chrono::milliseconds(500));

		logRotateByTime();
	}

	if(web3.ethereum()->isMining()) {
		web3.ethereum()->stopSealing();
	}

	if(jsonrpcHttpServer) {
		jsonrpcHttpServer->StopListening();
	}
	if (channelModularServer) {
		channelModularServer->StopListening();
	}
	if(chainParams.getInitializer()->consoleInitializer()->consoleServer()) {
		chainParams.getInitializer()->consoleInitializer()->consoleServer()->StopListening();
	}

	/*	auto netData = web3.saveNetwork();
		if (!netData.empty())
			writeFile(getDataDir() + "/network.rlp", netData);*/
	return 0;
}
