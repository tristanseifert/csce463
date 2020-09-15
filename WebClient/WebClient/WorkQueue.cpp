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