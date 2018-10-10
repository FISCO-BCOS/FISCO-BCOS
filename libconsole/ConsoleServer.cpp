/*
 * ConsoleServer.cpp
 *
 *  Created on: 2018年8月15日
 *      Author: monan
 */

#include "ConsoleServer.h"
#include <libpbftseal/PBFT.h>
#include <boost/throw_exception.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <libp2p/Common.h>
#include <libp2p/Host.h>
#include <libdevcore/FixedHash.h>
#include <libchannelserver/ChannelSession.h>
#include <libstorage/DB.h>
#include <libethcore/ABI.h>
#include <libdevcore/FileSystem.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>        
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <map>

using namespace dev;
using namespace dev::p2p;
using namespace console;

ConsoleServer::ConsoleServer() {
}

void ConsoleServer::StartListening() {
  try {
    LOG(DEBUG)<< "ConsoleServer started";

    std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp = std::bind(&ConsoleServer::onConnect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
    _server->setConnectionHandler(fp);

    _server->run();

    _running = true;
  }
  catch(std::exception &e) {
    LOG(ERROR) << "StartListening failed! " << boost::diagnostic_information(e);

    BOOST_THROW_EXCEPTION(e);
  }
}

void ConsoleServer::StopListening() {
  if (_running) {
    _server->stop();
  }

  _running = false;
}

void ConsoleServer::onConnect(dev::channel::ChannelException e,
                              dev::channel::ChannelSession::Ptr session) {
  if (e.errorCode() != 0) {
    LOG(ERROR)<< "Accept console connection error!" << boost::diagnostic_information(e);

    return;
  }

  std::function<void(dev::channel::ChannelSession::Ptr, dev::channel::ChannelException, dev::channel::Message::Ptr)> fp = std::bind(
      &ConsoleServer::onRequest,
      shared_from_this(),
      std::placeholders::_1,
      std::placeholders::_2,
      std::placeholders::_3);
  session->setMessageHandler(fp);

  session->run();
  std::string output;
  std::stringstream ss;
  ss << std::endl;
  printDoubleLine(ss);
  ss << "Welcome to the FISCO BCOS console!" << std::endl;
  ss << "Version: " << dev::Version << " " << dev::Copyright<< std::endl << std::endl;
  ss << "Type 'help' for command list. Type 'quit' to quit the console." << std::endl;
  printDoubleLine(ss);
  ss << std::endl;

  output = ss.str();
  auto response = session->messageFactory()->buildMessage();
  response->setData((byte*)output.data(), output.size());

  session->asyncSendMessage(response, dev::channel::ChannelSession::CallbackType(), 0);

}

void ConsoleServer::setKey(KeyPair key) {
  _key = key;
}

void ConsoleServer::onRequest(dev::channel::ChannelSession::Ptr session,
                              dev::channel::ChannelException e,
                              dev::channel::Message::Ptr message) {
  if (e.errorCode() != 0) {
    LOG(ERROR)<< "ConsoleServer onRequest error: " << boost::diagnostic_information(e);

    return;
  }

  std::string output;
  try
  {
    std::string request((const char*)message->data(), message->dataSize());
    LOG(TRACE) << "ConsoleServer onRequest: " << request;

    std::vector<std::string> args;
    boost::split(args, request, boost::is_any_of(" "));

    if (args.empty())
    {
      output = "Empty input!";
      throw std::exception();
    }

    std::string func = args[0];
    args.erase(args.begin());

    if (func == "help")
    {
      output = help(args);
    }
    else if (func == "status")
    {
      output = status(args);
    }
    else if (func == "p2p.list")
    {
      output = p2pList(args);
    }
    else if (func == "p2p.update")
    {
      output = p2pUpdate(args);
    }
    else if (func == "pbft.list")
    {
      output = pbftList(args);
    }
    else if (func == "pbft.add")
    {
      output = pbftAdd(args);
    }
    else if (func == "pbft.remove")
    {
      output = pbftRemove(args);
    }
    else if (func == "amdb.select")
    {
      output = amdbSelect(args);
    }
    else if (func == "quit")
    {
      session->disconnectByQuit();
    }
    else
    {
      output = "Unknown command, enter 'help' for command list.\n\n";
    }
  }
  catch (std::exception& e)
  {
    LOG(WARNING) << "Error: " << boost::diagnostic_information(e);
  }

  auto response = session->messageFactory()->buildMessage();
  response->setData((byte*)output.data(), output.size());

  session->asyncSendMessage(response, dev::channel::ChannelSession::CallbackType(), 0);

}

std::string ConsoleServer::help(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;

  printDoubleLine(ss);
  ss << "status          Show the blockchain status." << std::endl;
  ss << "p2p.list        Show the peers information." << std::endl;
  ss << "p2p.update      Update p2p nodes." << std::endl;
  ss << "pbft.list       Show pbft consensus node list." << std::endl;
  ss << "pbft.add        Add pbft consensus node." << std::endl;
  ss << "pbft.remove     Remove pbft consensus node." << std::endl;
  ss << "amdb.select     Query the table data." << std::endl;
  ss << "quit            Quit the blockchain console." << std::endl;
  ss << "help            Provide help information for blockchain console."
     << std::endl;
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::status(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;

  try {

    printDoubleLine(ss);
    ss << "Status: ";
    if (_interface->wouldSeal()) {
      ss << "sealing";
    } else if (_interface->isSyncing() || _interface->isMajorSyncing()) {
      ss << "syncing block...";
    }

    ss << std::endl;

    ss << "Block number:" << _interface->number() << " at view:"
        << dynamic_cast<dev::eth::PBFT*>(_interface->sealEngine())->view();
    ss << std::endl;
  } catch (std::exception &e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while status | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();

  return output;
}

std::string ConsoleServer::p2pList(const std::vector<std::string> args) {

	std::string output;
	std::stringstream ss;

	try {

		printDoubleLine(ss);
		ss << "Peers number: ";
		auto sessions = _host->mSessions();
		ss << sessions.size() << std::endl;
		for (auto it = sessions.begin(); it != sessions.end(); ++it) {
			auto s = it->second.lock();
			if(s) {
				printShortSingleLine(ss);
				const p2p::NodeID nodeid = s->id();
				ss << "Nodeid: " << (nodeid.hex()).substr(0, 8) << "..." << std::endl;
				ss << "Ip: " << s->peer()->endpoint.address << std::endl;
				ss << "Port:" << s->peer()->endpoint.tcpPort << std::endl;
			}
		}
	}
	catch(std::exception &e) {
		LOG(ERROR) << "ERROR: " << boost::diagnostic_information(e);
		ss << "ERROR while p2p.list | ";
		ss << e.what();
	}
	printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::pbftList(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;

  try {

    printDoubleLine(ss);
    ss << "Consensus nodes number: ";
    dev::h512s pbftList =
        dynamic_cast<dev::eth::PBFT*>(_interface->sealEngine())
            ->getMinerNodeList();
    ss << pbftList.size() << std::endl;
    std::vector<h512>::iterator iter;
    for(auto it = pbftList.begin(); it != pbftList.end(); ++it)
    {
      auto session = _host->peerSession(*it);

      if(session)
      { printShortSingleLine(ss);
        ss << "NodeId: " << (session->id().hex()).substr(0, 8) << "..." << std::endl;
        ss << "IP: " << session->peer()->endpoint.address << std::endl;
        ss << "Port:" << session->peer()->endpoint.tcpPort << std::endl;
        ss << "Online: ";
        if (_host->isConnected(session->id())) {
                   ss << "true";
        } else {
                   ss << "false";
        }
        ss << std::endl;
      }
      else
      {
         if(*it != _host->id())
         {   printShortSingleLine(ss);
             ss << "NodeId: " << ((*it).hex()).substr(0, 8) << "..." << std::endl;
             ss << "Online: false";
             ss << std::endl;
         }
      }

    }
    printShortSingleLine(ss);
    //local node
    const p2p::NodeID nodeid = _host->id();
    iter = find(pbftList.begin(), pbftList.end(), nodeid);
    if (iter != pbftList.end()) {
      ss << "NodeId(local): " << (nodeid.hex()).substr(0, 8) << "..."
         << std::endl;
      ss << "IP: " << _host->listenAddress() << std::endl;
      ss << "Port:" << _host->listenPort() << std::endl;
    }
  } catch (std::exception &e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while pbft.list | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::p2pUpdate(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;

  try {
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(getConfigPath(), pt);
    std::map<NodeIPEndpoint, NodeID> nodes;
    printDoubleLine(ss);
    ss << "Add p2p nodes: " << std::endl;
    printShortSingleLine(ss);
    for (auto it : pt.get_child("p2p")) {
      if (it.first.find("node.") == 0) {
        ss << it.first << " : " << it.second.data() << std::endl;

        std::vector<std::string> s;
        try {
          boost::split(s, it.second.data(), boost::is_any_of(":"),
                       boost::token_compress_on);

          if (s.size() != 2) {
            LOG(ERROR)<< "parse address failed:" << it.second.data();
            continue;
          }

          NodeIPEndpoint endpoint;
          endpoint.address = boost::asio::ip::address::from_string(s[0]);
          endpoint.tcpPort = boost::lexical_cast<uint16_t>(s[1]);

          nodes.insert(std::make_pair(endpoint, NodeID()));
        } catch (std::exception &e) {
          LOG(ERROR)<< "Parse address faield:" << it.second.data() << ", " << e.what();
          continue;
        }
      }
    }
    _host->setStaticNodes(nodes);
    ss << "Update p2p nodes successfully！" << std::endl;
  } catch (std::exception &e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while p2p.update | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::amdbSelect(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;

  try {
    if (args.size() == 2 && args[0] != "" && args[1] != "") {
      std::string tableName = args[0];
      std::string key = args[1];
      unsigned int number = _interface->number();
      h256 hash = _interface->hashFromNumber(number);
      dev::storage::Entries::Ptr entries = _stateStorage->select(hash,
                                                                 number + 1,
                                                                 tableName,
                                                                 key);
      size_t size = entries->size();
      printDoubleLine(ss);
      ss << "Number of entry: " << size << std::endl;
      for (size_t i = 0; i < size; ++i) {
        printShortSingleLine(ss);
        storage::Entry::Ptr entry = entries->get(i);
        std::map<std::string, std::string> *fields = entry->fields();
        std::map<std::string, std::string>::iterator it;
        for (it = fields->begin(); it != fields->end(); it++) {
          ss << it->first << ": " << it->second << std::endl;
        }
      }
    } else {
      ss << "You must specify table name and table key, for example"
         << std::endl;
      ss << "amdb.select t_test fruit" << std::endl;
    }
  } catch (std::exception &e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while amdb.select | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::pbftAdd(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;
  try {
    printDoubleLine(ss);
    if (args.size() == 1 && args[0] != "") {
      std::string nodeID = args[0];
      if (nodeID.size() != 128u)
      {
        ss << "NodeId length error!" << std::endl;
        printSingleLine(ss);
        ss << std::endl;
        return ss.str();
      }
      TransactionSkeleton t;
      //   ret.from = _key.address();
      t.to = Address(0x1003);
      t.creation = false;
      static boost::uuids::random_generator uuidGenerator;
      std::string s = to_string(uuidGenerator());
      s.erase(boost::remove_if(s, boost::is_any_of("-")), s.end());
      t.randomid = u256("0x" + s);
      t.blockLimit = u256(_interface->number() + 100);
      dev::eth::ContractABI abi;
      t.data = abi.abiIn("add(string)", nodeID);
      _interface->submitTransaction(t, _key.secret());
      ss << "Tx(Add consensus node) send successfully!" << endl;
    } else {
      ss << "You must specify nodeID, for example" << std::endl;
      ss << "pbft.add 123456789..." << std::endl;
    }
  } catch (std::exception& e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while pbft.add | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

std::string ConsoleServer::pbftRemove(const std::vector<std::string> args) {
  std::string output;
  std::stringstream ss;
  try {
    printDoubleLine(ss);
    if (args.size() == 1 && args[0] != "") {
      std::string nodeID = args[0];
      if (nodeID.size() != 128u)
      {
        ss << "NodeId length error!" << std::endl;
        printSingleLine(ss);
        ss << std::endl;
        return ss.str();
      }
      TransactionSkeleton t;
      t.to = Address(0x1003);
      t.creation = false;
      static boost::uuids::random_generator uuidGenerator;
      std::string s = to_string(uuidGenerator());
      s.erase(boost::remove_if(s, boost::is_any_of("-")), s.end());
      t.randomid = u256("0x" + s);
      t.blockLimit = u256(_interface->number() + 100);
      dev::eth::ContractABI abi;
      t.data = abi.abiIn("remove(string)", nodeID);
      _interface->submitTransaction(t, _key.secret());
      ss << "Tx(Remove consensus node) send successfully! " << endl;
    } else {
      ss << "You must specify nodeID, for example" << std::endl;
      ss << "pbft.remove 123456789..." << std::endl;
    }
  } catch (std::exception& e) {
    LOG(ERROR)<< "ERROR: " << boost::diagnostic_information(e);
    ss << "ERROR while pbft.remove | ";
    ss << e.what();
  }
  printSingleLine(ss);
  ss << std::endl;

  output = ss.str();
  return output;
}

void ConsoleServer::printShortSingleLine(std::stringstream &ss) {
  ss << "------------------------------------" << std::endl;
}
void ConsoleServer::printSingleLine(std::stringstream &ss) {
  ss << "------------------------------------------------------------------------"
     << std::endl;
}

void ConsoleServer::printDoubleLine(std::stringstream &ss) {
  ss << "========================================================================"
     << std::endl;
}
