#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <cstddef>
#include <string>
#include <queue>
#include <unordered_set>
#include <functional>

namespace webclient {
class WorkQueue {
private:
    WorkQueue();
    ~WorkQueue();
    
public:
    /// add a new URL string to the queue
    void pushUrlString(const std::string&);
    /// add a new URL string to the queue WITHOUT locking (unsafe!)
    void pushUrlStringUnsafe(const std::string& str);
    /// gets an URL string from the head of queue if any
    bool popUrlString(std::string&);

    /// Accesses the hosts set
    void lockedHostsAccess(std::function<void(std::unordered_set<std::string>&)>);
    /// Accesses the addresses set
    void lockedAddrAccess(std::function<void(std::unordered_set<std::string>&)>);

private:
    /// protect access to URL string queue
    CRITICAL_SECTION urlLock;
    /// queue of URL strings
    std::queue<std::string> urlQueue;

    /// protect access to hosts set
    CRITICAL_SECTION hostsLock;
    /// set of all visited hostnames
    std::unordered_set<std::string> hosts;

    /// protect access to addresses set
    CRITICAL_SECTION addressesLock;
    /// set of all visited addresses
    std::unordered_set<std::string> addresses;

public:
    static WorkQueue shared;
};
}

#endif