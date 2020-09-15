#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <cstddef>
#include <string>
#include <queue>

#include <Windows.h>

namespace webclient {
class WorkQueue {
private:
    WorkQueue();
    
public:
    /// add a new URL string to the queue
    void pushUrlString(const std::string&);
    /// add a new URL string to the queue WITHOUT locking (unsafe!)
    void pushUrlStringUnsafe(const std::string& str);
    /// gets an URL string from the head of queue if any
    bool popUrlString(std::string&);

private:
    /// protect access to URL string queue
    CRITICAL_SECTION urlLock;
    /// queue of URL strings
    std::queue<std::string> urlQueue;

public:
    static WorkQueue shared;
};
}

#endif