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
 * @file: RPCallback.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <unordered_map>
#include <libethereum/Block.h>
#include <libdevcore/Guards.h>
#include <libdevcore/Worker.h>
#include <libchannelserver/ChannelSession.h>
#include <libweb3jsonrpc/AccountHolder.h>

using namespace std;
using namespace dev::channel;
using namespace dev::eth;

namespace dev {
    namespace eth {
        
        struct SeqSessionInfo {
            ChannelSession::Ptr session;
            string seq;
        };
        
        typedef std::shared_ptr<SeqSessionInfo> SSPtr;
        
        class RPCallback {
        public:
            //instance
            static RPCallback& getInstance();
            
            //keep hash->session mapping
            bool parseAndSaveSession(const string& jsonReqStr, const string& seq, ChannelSession::Ptr session);
            
            //get session
            SSPtr getSessionInfoByHash(std::string hash);
            
        private:
            RPCallback();
            unordered_map<std::string, SSPtr> m_hashSessionMap;
            SharedMutex x_map;
            SharedMutex x_sessionMap;
        };
        
        class CallbackWorker : public Worker {
        public:
            virtual ~CallbackWorker(){};
            static const uint MAX_SZIE = 100;
            static CallbackWorker& getInstance();
            virtual void doWork() override;
            bool addBlock(shared_ptr<Block> _block);
        private:
            CallbackWorker(): Worker("CallbackWorker", 20){
                startWorking();
            }
            void checkIf(shared_ptr<Block> _block);
            
            vector<std::shared_ptr<Block>> m_blocks;
            SharedMutex x_block;
        };
    }
}

