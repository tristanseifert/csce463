#ifndef STATSTHREAD_H
#define STATSTHREAD_H

#include <chrono>
#include <atomic>

/**
 * Stats thread handles printing info about the total number of URLs crawled, and so on
 * periodically while data is being fetched.
*/
namespace webclient {
DWORD WINAPI StatsThreadEntry(LPVOID);

class StatsThread {
    friend DWORD WINAPI StatsThreadEntry(LPVOID);

public:
    /// Signals the stats thread to quit
    static void quit()
    {
        shared._signalQuit();
    }
    /// Starts the stats thread
    static void begin()
    {
        shared._startThread();
    }

private:
    StatsThread();
    ~StatsThread();

    void _startThread();
    void _signalQuit();

    void threadMain();

    void recalculateBandwidth();
    void printFinalStats();

private:
    // handle to the stats thread
    HANDLE thread = INVALID_HANDLE_VALUE;
    // signalled when quit is desired
    HANDLE quitEvent = INVALID_HANDLE_VALUE;

    // start time of thread, in seconds
    std::chrono::time_point<std::chrono::steady_clock> startTime;

    /// last time bandwidth was calculated
    std::chrono::time_point<std::chrono::steady_clock> bandwidthTime;
    /// number of bytes that were received the last time bandwidth was calculated
    unsigned long bandwidthBytes = 0;
    /// number of pages crawled the last time the bandwidth was calculated
    unsigned long bandwidthPages = 0;
    /// bandwidth (in bps)
    double bandwidth = 0;
    /// crawling speed (in pages per sec)
    double crawlSpeed = 0;

private:
    /// size of the stats thread stack, in bytes.
    constexpr static const size_t kStackSize = (1024 * 64);

public:
    // current state
    // we should have accessor methods for this but we don't in the interest of brevity
    struct {
        /// number of worker threads currently running
        std::atomic_ulong numThreads = 0;

        /// number of items in the work queue (Q)
        std::atomic_ulong queueSize = 0;
        /// number of items of work that have been processed (E)
        std::atomic_ulong queuePulled = 0;

        /// count of URLs with unique hosts (H)
        std::atomic_ulong uniqueHosts = 0;
        /// count of successful DNS lookups (D)
        std::atomic_ulong numDnsLookups = 0;

        /// number of unique IPs (I)
        std::atomic_ulong uniqueIps = 0;
        /// number of robots files requested
        std::atomic_ulong robotsAttempted = 0;
        /// count of URLs that passed robot checks (R)
        std::atomic_ulong robotsCheckPassed = 0;
        /// number of content pages requested
        std::atomic_ulong contentPagesAttempted = 0;
        /// count of successfully crawled URLs (C)
        std::atomic_ulong successPages = 0;
        /// total number of links found (L)
        std::atomic_ulong numLinks = 0;

        /// Total number of HTTP requests made
        std::atomic_ulong numRequests = 0;

        /// Total number of bytes received
        std::atomic_ulong bytesRx = 0;
        /// Total number of bytes sent
        std::atomic_ulong bytesTx = 0;

        /// number of each type of HTTP code (1xx = 1, 2xx = 2, .. 5xx = 5, 0 is "others")
        std::atomic_ulong httpStatusCodes[6] = { 0, 0, 0, 0, 0, 0 };
    } state;

public:
    static StatsThread shared;
};
}

#endif