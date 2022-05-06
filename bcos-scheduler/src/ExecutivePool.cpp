//
// Created by Jimmy Shi on 2022/5/5.
//

#include "ExecutivePool.h"

using namespace bcos::scheduler;

void ExecutivePool::add(ContextID contextID, ExecutiveState::Ptr executiveState)
{
    m_pendingExecutives.insert({contextID, executiveState});
    m_needPrepare->insert(contextID);
}


ExecutiveState::Ptr ExecutivePool::get(ContextID contextID)
{
    auto it = m_pendingExecutives.find(contextID);
    if (it != m_pendingExecutives.end())
    {
        return it->second;
    }
    else
    {
        return nullptr;
    }
};

void ExecutivePool::markAs(ContextID contextID, MessageHint type)
{
    switch (type)
    {
    case NEED_PREPARE:
    {
        m_needPrepare->insert(contextID);
        break;
    }
    case LOCKED:
    {
        m_hasLocked->insert(contextID);
        break;
    }
    case NEED_SEND:
    {
        m_needSend->insert(contextID);
        break;
    }
    case NEED_SCHEDULE_OUT:
    {
        m_needScheduleOut->insert(contextID);
        break;
    }
    case END:
    {
        m_needRemove->insert(contextID);
        break;
    }
    case ALL:  // do nothing
    default:
        break;
    }
}

void ExecutivePool::refresh()
{
    // handle need send
    for (auto contextID : *m_needSend)
    {
        m_hasLocked->unsafe_erase(contextID);
    }

    // remove all need remove
    for (auto contextID : *m_needRemove)
    {
        m_pendingExecutives.unsafe_erase(contextID);
        m_hasLocked->unsafe_erase(contextID);
        m_needPrepare->unsafe_erase(contextID);
    }
    m_needRemove->clear();

    for (auto contextID : *m_needScheduleOut)
    {
        m_pendingExecutives.unsafe_erase(contextID);
        m_hasLocked->unsafe_erase(contextID);
        m_needPrepare->unsafe_erase(contextID);
    }
    m_needScheduleOut->clear();
}

bool ExecutivePool::empty(MessageHint type)
{
    switch (type)
    {
    case NEED_PREPARE:
    {
        return m_needPrepare->empty();
    }
    case LOCKED:
    {
        return m_hasLocked->empty();
    }
    case NEED_SEND:
    {
        return m_needSend->empty();
    }
    case NEED_SCHEDULE_OUT:
    {
        return m_needScheduleOut->empty();
    }
    case END:
    {
        return m_needRemove->empty();
    }
    case ALL:
    {
        return m_pendingExecutives.empty();
    }
    }
}


void ExecutivePool::forEach(MessageHint type, ExecutiveStateHandler handler, bool needClear)
{
    SetPtr pool = nullptr;
    switch (type)
    {
    case NEED_PREPARE:
    {
        pool = m_needPrepare;
        break;
    }
    case LOCKED:
    {
        pool = m_hasLocked;
        break;
    }
    case NEED_SEND:
    {
        pool = m_needSend;
        break;
    }
    case END:
    {
        pool = m_needRemove;
        break;
    }
    case NEED_SCHEDULE_OUT:
    {
        pool = m_needScheduleOut;
        break;
    }
    case ALL:
    {
        SetPtr allID = std::make_shared<tbb::concurrent_set<ContextID>>();
        for (auto it : m_pendingExecutives)
        {
            allID->insert(it.first);
        }
        pool = allID;
        break;
    }
    default:
        break;
    }

    if (pool == nullptr)
    {
        return;
    }

    for (auto contextID : *pool)
    {
        auto executiveState = get(contextID);
        if (executiveState != nullptr)
        {
            if (!handler(contextID, executiveState))
            {
                // break if no need to contiue
                break;
            }
        }
    }

    if (needClear)
    {
        pool->clear();
    }
}

void ExecutivePool::forEachAndClear(MessageHint type, ExecutiveStateHandler handler)
{
    forEach(type, std::move(handler), true);
}
