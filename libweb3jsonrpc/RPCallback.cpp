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
 * @file: RPCallback.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <libweb3jsonrpc/JsonHelper.h>
#include <libethereum/Transaction.h>
#include <libweb3jsonrpc/RPCallback.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcore/CommonJS.h>

using namespace dev;
using namespace dev::eth;

RPCallback& RPCallback::getInstance() {
    static RPCallback instance;
    return instance;
}

RPCallback::RPCallback() {    
}

/**
 *  @解析json的rpc请求，get出来其中的method和计算sha3
 *
 **/
bool RPCallback::parseAndSaveSession(const string& jsonReqStr, const string& seq, ChannelSession::Ptr session) {
    Json::Reader reader;
    Json::Value req;
    Json::Value root;
    
    const string keyMethod = "method";
    const string keyParams = "params";
    const string sendTransaction = "eth_sendTransaction";
    const string sendRawTransaction = "eth_sendRawTransaction";
    
    if (!reader.parse(jsonReqStr, req, false)) {
        LOG(WARNING) << "parse jsonReqStr error.jsonReqStr:" << jsonReqStr;
        return false;
    }
    
    if (req.isObject()) {
        root.append(req);
    } else if (req.isArray()) {
        root = req;
    } else {
        LOG(WARNING) << "req is not json object or array. jsonReqStr:" << jsonReqStr;
        return false;
    }
    
    for (unsigned int i = 0; i < root.size(); i++) {
        try {
            if (!root[i].isMember(keyMethod) || !root[i][keyMethod].isString()) {
                LOG(WARNING) << "jsonReqStr method error. jsonReqStr:" << jsonReqStr;
                continue;
            }
            
            std::string method = root[i][keyMethod].asString();
            if (method != sendTransaction && method != sendRawTransaction) {
                continue;
            }
            
            if (!root[i].isMember(keyParams) || !root[i][keyParams].isArray()) {
                LOG(WARNING) << "jsonReqStr params error. jsonReqStr:" << jsonReqStr;
                continue;
            }
            
            
            Json::Value paramsArr = root[i][keyParams];
            h256 hash;
            if (method == sendRawTransaction) {
                const string params = paramsArr[0u].asString();
                auto tx_data = jsToBytes(params, OnFailed::Throw);
                hash = sha3(tx_data);
            }
            
            string hashStr = hash.hex();
            if (hashStr.empty()) {
                continue;
            }
            
            unordered_map<std::string, SSPtr>::iterator it;
            DEV_READ_GUARDED(x_sessionMap) {
                it = m_hashSessionMap.find(hashStr);
            }
            
            if (it != m_hashSessionMap.end()) {
                continue;
            } else {
                SSPtr ssPtr = std::make_shared<SeqSessionInfo>();
                ssPtr->seq = seq;
                ssPtr->session = session;
                DEV_WRITE_GUARDED(x_sessionMap) {
                    m_hashSessionMap.emplace(hashStr, ssPtr);
                }
            }
        } catch(std::exception& e) {
            LOG(ERROR) << "parseAndSaveSession exception:" << e.what();
        } catch(...) {
            LOG(ERROR) << "parseAndSaveSession unknown exception";
        }
    }
    
    return true;
}

SSPtr RPCallback::getSessionInfoByHash(std::string hash) {
    unordered_map<std::string, SSPtr>::iterator it;
    DEV_READ_GUARDED(x_sessionMap) {
        it = m_hashSessionMap.find(hash);
    }
    
    if (it == m_hashSessionMap.end()) {
        return NULL;
    } else {
        SSPtr ptr = it->second;
        DEV_WRITE_GUARDED(x_sessionMap) {
            m_hashSessionMap.erase(it);
        }
        
        return ptr;
    }
}

CallbackWorker& CallbackWorker::getInstance() {
    static CallbackWorker callbackWorker;
    return callbackWorker;
}

void CallbackWorker::doWork() {
    if (m_blocks.size() == 0) {
        return;
    }
    
    vector<std::shared_ptr<Block>> blocks;
    
    DEV_WRITE_GUARDED(x_block) {
        blocks.swap(m_blocks);
    }
    
    int size = blocks.size();
    for(int i = 0; i < size; i++) {
        shared_ptr<Block> pBlock = blocks[i];
        checkIf(pBlock);
    }
}

bool CallbackWorker::addBlock(shared_ptr<Block> _block) {
    if (m_blocks.size() > MAX_SZIE) {
        return false;
    }
    
    DEV_WRITE_GUARDED(x_block) {
        m_blocks.push_back(_block);
    }
    
    return true;
}

void CallbackWorker::checkIf(shared_ptr<Block> _block) {
    if (!_block) {
        return;
    }
    
    unsigned blockNumber = (unsigned)_block->info().number();
    h256 blockHash = _block->info().hash();
    unsigned int index = 0;
    
    for (Transaction t : _block->pending()) {
        
        try {
            string hash = t.sha3().hex();
            SSPtr ssPtr = RPCallback::getInstance().getSessionInfoByHash(hash);
            if (ssPtr != NULL && ssPtr->session != NULL) {
                dev::channel::Message::Ptr message = make_shared<dev::channel::Message>();
                message->setSeq(ssPtr->seq);
                message->setResult(0);
                message->setType(0x1000);//交易成功的type,跟java sdk保持一致
                TransactionReceipt tr = _block->receipt(index);
                Json::Value jsonValue = toJson(LocalisedTransactionReceipt(tr, t.sha3(), blockHash, blockNumber, index));
                Json::FastWriter fastWriter;
                std::string jsonValueStr = fastWriter.write(jsonValue);
                message->setData((const byte*)jsonValueStr.data(), jsonValueStr.size());
                LOG(DEBUG) << "start to asyncSendMessage in CallbackWorker.hash:" << hash << ",receipt:" << jsonValueStr;
                ssPtr->session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
            }
        } catch(std::exception& e) {
            LOG(ERROR) << "CallbackWorker checkIf exception:" << e.what();
            //add error log
        } catch(...) {
            LOG(ERROR) << "CallbackWorker checkIf unknown exception";
        }
        
        index++;
    }
    
}
