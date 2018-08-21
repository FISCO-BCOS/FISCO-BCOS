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
 * @file: StatLog.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <memory>
#include <libdevcore/Common.h>
#include <libdevcore/LogGuard.h>
#include <libdevcore/Guards.h>

namespace std 
{
    template<typename T, typename ...Args>
    std::unique_ptr<T> make_unique( Args&& ...args )
    {
        return std::unique_ptr<T>( new T( std::forward<Args>(args)... ) );
    }
}

namespace dev
{
namespace eth 
{


class StatTxExecLogGuard : public TimeIntervalLogGuard 
{
public:
    StatTxExecLogGuard() : TimeIntervalLogGuard(StatCode::TX_EXEC, STAT_TX_EXEC, report_interval) 
    { }

    void txExecFailed() { m_success = 0; }

    static uint64_t report_interval;

};

class StatLogContext;
class StatLogState
{
public:
     virtual void handle(StatLogContext& context, const std::string& str, void* ext = 0) = 0;
};

class StatLogContext : public LogOutputStreamBase
{
protected:
    StatLogContext(u256 _id, const std::string& _tag, std::unique_ptr<StatLogState>&& _state, bool is_valid = true) : 
        LogOutputStreamBase(),
        m_id {_id},
        m_is_valid { is_valid },
        m_code { _id.convert_to<int>() },
        m_tag { _tag },
        m_state { move(_state) }
    { }
    virtual ~StatLogContext() {}

public:
    std::string str() { return m_sstr.str(); }
    void changeState(std::unique_ptr<StatLogState>&& _state) { m_state = move(_state); }
	void request(const std::string& str, void* ext = 0) { m_state->handle(*this, str, ext); }
    
    bool isFinalState() { return m_is_final; }
    bool isValidState() { return m_is_valid; }

    virtual void changeToFinalState() = 0;

    const u256& getId() const { return m_id; }
    const int& getCode() const { return m_code; }

protected:
    const u256 m_id;
    bool m_is_final = false;
    bool m_is_valid = true;
    int m_code;
    
private:
    std::string m_tag;
    std::unique_ptr<StatLogState> m_state;
};

// pbft state flow 
// start -> seal -> exec -> collectSign -> collectCommit -> blkToChain -> destory
// or start -> exec -> collectSign -> collectCommit -> blkToChain -> destory
class StartBlockState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void* ext = 0) override;
};

class SealedState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class ExecedState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class CollectedSignState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class CollectedCommitState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class BlkToChainState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class ViewChangeStartState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class ViewChangedState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class DestoriedState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

// context
class PBFTStatLog : public StatLogContext
{
public:
    PBFTStatLog(u256 _id = Invalid256, bool is_valid = true) : StatLogContext(_id, "pbft stat", std::make_unique<StartBlockState>(), is_valid) // init state
    {
    }

    void changeToFinalState() override
    {
        this->changeState(std::make_unique<DestoriedState>());
        m_is_final = true;
    }

    void changeToViewchangeState()
    {
        this->changeState(std::make_unique<ViewChangeStartState>());
    }
    // frind function
    friend void BlkToChainState::handle(StatLogContext&, const std::string&, void*);
    friend void ViewChangedState::handle(StatLogContext&, const std::string&, void*);
};

// tx tate flow
// init -> toChain -> destory
class TxInitState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class TxToChainState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

class TxDestoriedState : public StatLogState 
{
public:
    void handle(StatLogContext& context, const std::string& str, void*) override;
};

// context
class TxStatLog : public StatLogContext
{
public:
    TxStatLog(u256 _id = Invalid256, bool is_valid = true) : StatLogContext(_id, "tx stat", std::make_unique<TxInitState>(), is_valid) // init state
    {
    }
    void changeToFinalState() 
    {
        this->changeState(std::make_unique<TxDestoriedState>());
    }
    // frind function
    friend void TxToChainState::handle(StatLogContext&, const std::string&, void*);
};


// work flow container
template<class StatLog, size_t cap>
class StatLogContainer 
{
private:
    StatLogContainer() { m_capacity = cap; m_map.insert(make_pair(Invalid256, std::make_unique<StatLog>(Invalid256, false))); }
    StatLogContainer(const StatLogContainer&) = delete;
    mutable Mutex m_mutex;

private:
    std::unordered_map<u256, std::unique_ptr<StatLog>> m_map;
    std::list<u256> m_list;
    size_t m_capacity;

public:
    static StatLogContainer<StatLog, cap>& instance() 
    {
        static StatLogContainer<StatLog, cap> ins;
        return ins;
    }

    StatLog& logContext(const u256& hash) 
    {
        auto it = m_map.find(hash);
        if (it != m_map.end())
            return *it->second;
        return *m_map.at(Invalid256);
    }

    void log(const u256& hash, const std::string& str, void* ext=0, bool init = false) 
    {
        auto it = m_map.find(hash);
        if (it == m_map.end())
        {
            if (!init)
                return;
            // 开始初始统计
            Guard l(m_mutex);
            auto ret = m_map.insert(make_pair(hash, std::make_unique<StatLog>(hash)));
            if (!ret.second) return;
            it = ret.first;
            m_list.push_back(hash);
        }
        
        if(it != m_map.end() && it->second) 
        {
            it->second->request(str, ext); // exec handle
        }

        clearCache(hash);
    }

    void setCapacity(size_t c) { m_capacity = c; } 

private:
    void clearCache(const u256& hash) 
    {
        bool err = false;
        if (m_map.size() > m_capacity)
        {
            auto& front_hash = m_list.front(); 
            auto it = m_map.find(front_hash);
            if (it != m_map.end())
            {
                auto& context = it->second;
                if (!context->isFinalState()) 
                {
                    context->changeToFinalState();
                    err = true; 
                    context->request("over capacity", &err);  
                    m_map.erase(it);
                }
            }
            m_list.pop_front();
        }
        auto it = m_map.find(hash);
        if (it != m_map.end())
        {
        	auto& context = it->second;
            if (context->isFinalState()) 
            {
                err = false; 
                context->request("success!", &err); 
                m_map.erase(it);
                
                auto list_it = m_list.begin();
                for(;list_it != m_list.end(); list_it++)
                {
                    if (*list_it == hash)
                    {
                        m_list.erase(list_it);
                        break; 
                    }
                }
            }           
        }
    }
};

class LogFlowConstant 
{
public:
    static uint64_t PBFTReportInterval;
    static uint64_t TxReportInterval;
};

class LogConstant 
{
public:
    static uint64_t BroadcastTxInterval;
    static uint64_t BroadcastBlockInterval;
};

using DefaultPBFTStatLog = StatLogContainer<PBFTStatLog, 5>;
using DefaultTxtatLog = StatLogContainer<TxStatLog, 1024>;  // same to transaction queue cap
// todo for test
// using DefaultTxtatLog = StatLogContainer<TxStatLog, 20>;

void PBFTFlowLog(const u256& hash, const std::string& str, int code=0, bool is_init = false); 

void PBFTFlowViewChangeLog(const u256& hash, const std::string& str);

void TxFlowLog(const u256& hash, const std::string& str, bool is_discarded = false, bool is_init = false);

void BroadcastTxSizeLog(const h512 &node_id, size_t packet_size);

void BroadcastBlockSizeLog(const h512 &node_id, size_t packet_size);

}
}
