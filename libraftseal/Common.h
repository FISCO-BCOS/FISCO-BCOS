
#pragma once

#include <libdevcore/Common.h>
#include <mutex>
#include <condition_variable>
#include <boost/thread.hpp>

#define craft LOG(TRACE)

enum RaftMsgType
{
	RaftMsgRaft = 0x01,

	RaftMsgCount
};

enum RaftPacketType
{
	raftBlockVote = 0x01,
	raftBlockVoteEcho,
	RaftTestPacket,

	raftPacketCount
};

struct AutoLock
{
	AutoLock(boost::recursive_mutex & _lock): m_lock(_lock)
	{
		//m_lock.lock();
	}
	
	~AutoLock()
	{
		//m_lock.unlock();
	}

	boost::recursive_mutex & m_lock;
};

#define AUTO_LOCK(x) AutoLock autoLock(x)


