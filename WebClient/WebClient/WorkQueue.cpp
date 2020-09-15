#include "pch.h"
#include "WorkQueue.h"
#include "StatsThread.h"

using namespace webclient;

/// shared instance
WorkQueue WorkQueue::shared;

/**
 * @brief Initializes the work queue.
*/
WorkQueue::WorkQueue()
{
    InitializeCriticalSection(&this->urlLock);
    InitializeCriticalSection(&this->hostsLock);
    InitializeCriticalSection(&this->addressesLock);
}

/**
 * @brief Cleans up the allocated critical sections.
*/
WorkQueue::~WorkQueue()
{
    DeleteCriticalSection(&this->urlLock);
    DeleteCriticalSection(&this->hostsLock);
    DeleteCriticalSection(&this->addressesLock);
}

/**
 * @brief Pushes an URL string into the work queue.
 * @param str 
*/
void WorkQueue::pushUrlString(const std::string& str)
{
    EnterCriticalSection(&this->urlLock);
    this->pushUrlStringUnsafe(str);
    LeaveCriticalSection(&this->urlLock);
}


void WorkQueue::pushUrlStringUnsafe(const std::string& str)
{
    this->urlQueue.push(str);
    StatsThread::shared.state.queueSize++;
}

/**
 * @brief Pops a string from the head of the URL queue, if there is any.
 * @param outStr String popped from queue
 * @return Whether a string was popped (true) or the queue is empty (false)
*/
bool WorkQueue::popUrlString(std::string& outStr)
{
    bool found = false;

    // enter critical section
    EnterCriticalSection(&this->urlLock);

    // is the queue empty? if so, bail
    if (this->urlQueue.empty()) {
        goto mcdonalds;
    }
    // otherwise, pop the result
    outStr = this->urlQueue.front();
    
    this->urlQueue.pop();
    StatsThread::shared.state.queuePulled++;
    found = true;

mcdonalds:;
    // leave critical section and return status
    LeaveCriticalSection(&this->urlLock);
    return found;
}

/**
 * @brief Allows access the  hosts set with locking around it.
 * @param dude Function invoked. The lock is acquired and held for the duration of the function.
*/
void WorkQueue::lockedHostsAccess(std::function<void(std::unordered_set<std::string>&)> dude)
{
    EnterCriticalSection(&this->hostsLock);
    dude(this->hosts);
    LeaveCriticalSection(&this->hostsLock);
}

/**
 * @brief Allows access the addresses set with locking around it.
 * @param dude Function invoked. The lock is acquired and held for the duration of the function.
*/
void WorkQueue::lockedAddrAccess(std::function<void(std::unordered_set<std::string>&)> dude)
{
    EnterCriticalSection(&this->addressesLock);
    dude(this->addresses);
    LeaveCriticalSection(&this->addressesLock);
}