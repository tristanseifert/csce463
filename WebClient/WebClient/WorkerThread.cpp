#include "pch.h"
#include "WorkerThread.h"
#include "WorkQueue.h"
#include "StatsThread.h"
#include "URL.h"
#include "HTTPClient.h"

#include <string>
#include <stdexcept>
#include <iostream>

using namespace webclient;

/**
 * @brief Allocates the worker's thread, but does not start it yet.
*/
WorkerThread::WorkerThread()
{
    // set up thread
    this->thread = CreateThread(nullptr, kStackSize, WorkerThreadEntry, this, CREATE_SUSPENDED, nullptr);
}

/**
 * @brief Cleans up the thread if still running.
*/
WorkerThread::~WorkerThread()
{
    // kill off thread
    if (this->thread != INVALID_HANDLE_VALUE) {
        // signal quit
        this->_signalQuit();
        // do NOT call TerminateThread(); wait for it to quit and destroy it
        WaitForSingleObject(this->thread, 500);
        CloseHandle(this->thread);
        this->thread = INVALID_HANDLE_VALUE;
    }

    // destroy events
    if (this->quitEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(this->quitEvent);
        this->quitEvent = INVALID_HANDLE_VALUE;
    }
}

/**
 * @brief Starts the worker thread.
*/
void WorkerThread::_startThread()
{
    ResumeThread(this->thread);
}

/**
 * @brief Indicates to the stats thread it should quit.
*/
void WorkerThread::_signalQuit()
{
    if (this->quitEvent != INVALID_HANDLE_VALUE && !SetEvent(this->quitEvent)) {
        std::string msg = "SetEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }
}

/**
 * @brief Waits indefinitely for the worker thread to exit.
*/
void WorkerThread::_waitForQuit()
{
    WaitForSingleObject(this->thread, INFINITE);
}


/**
 * @brief Trampoline to jump into the class main method
 * @param ctx Context passed to thread creation
*/
DWORD WINAPI webclient::WorkerThreadEntry(LPVOID ctx)
{
    static_cast<WorkerThread*>(ctx)->threadMain();
    return 0;
}
/**
 * Thread entry point
 */
void WorkerThread::threadMain()
{
    // starting
    StatsThread::shared.state.numThreads++;

    // pop work until done
    std::string urlStr;
    while (WorkQueue::shared.popUrlString(urlStr)) {
        try {
            this->processUrl(urlStr);
        } catch (std::exception& e) {
//            std::cerr << "Failed to process '" << urlStr << "': " << e.what() << std::endl
//                      << std::flush; 
        }
    }

    // exiting
    StatsThread::shared.state.numThreads--;
}

/**
 * @brief Processes a single url.
 * @param urlStr URL string to parse
*/
void WorkerThread::processUrl(const std::string& urlStr)
{
    URL url;
    struct sockaddr_storage addr = { 0 };

    // parse the URL and validate scheme
    url = URL(urlStr);
    if (url.getScheme() != "http") {
        throw std::runtime_error("Invalid scheme");
    }
    // check that the host is unique
    if (!this->checkHostUniqueness(url)) {
        std::cerr << "Failed host uniqueness test: " << url.toString() << std::endl;
        return;
    }
    // resolve the host, and validate the IP is unique
    if (!this->checkAddressUniqueness(url, &addr)) {
        std::cerr << "Failed IP uniqueness test: " << url.toString() << std::endl;
        return;
    }
}

/**
 * @brief Determines whether the hostname of the given URL is unique.
 * @param url URL to check
 * @return If the hostname has been encountered before (false) or it's unique (true)
*/
bool WorkerThread::checkHostUniqueness(const URL& url)
{
    bool unique = false;

    // perform a locked operation against the set
    WorkQueue::shared.lockedHostsAccess([&url, &unique](auto hosts) {
        if (hosts.find(url.getHostname()) != hosts.end()) {
            unique = false;
        } else {
            hosts.insert(url.getHostname());

            StatsThread::shared.state.uniqueHosts++;
            unique = true;
        }
    });

    return unique;
}

/**
 * @brief Resolves the given URL and checks whether the resultant IP address is unique.
 * @param url URL to check
 * @param addr Buffer to write resolved hostname into
 * @return Whether the address is unique (true) or not
*/
bool WorkerThread::checkAddressUniqueness(URL& url, struct sockaddr_storage* addr)
{
    using namespace std;

    bool unique = false;
    std::string addrString;

    // perform DNS resolution
    auto start = std::chrono::steady_clock::now();

    url.resolve(addr);

    // calculate total time taken and print result
    auto end = chrono::steady_clock::now();
    auto diff = end - start;
    auto ms = chrono::duration<double, milli>(diff).count();

    char buffer[INET6_ADDRSTRLEN] = { 0 };

    if (addr->ss_family == AF_INET) {
        auto addrV4 = reinterpret_cast<sockaddr_in*>(addr);
        auto out = inet_ntop(addrV4->sin_family, &addrV4->sin_addr, buffer,
            INET6_ADDRSTRLEN);
        addrString = string(out);
    }

    // then, ensure the IP is unique
    WorkQueue::shared.lockedAddrAccess([&addrString, &unique](auto ips) {
        if (ips.find(addrString) != ips.end()) {
            unique = false;
        } else {
            ips.insert(addrString);

            StatsThread::shared.state.uniqueIps++;
            unique = true;
        }
    });

    return unique;
}