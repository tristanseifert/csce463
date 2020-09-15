#include "pch.h"
#include "StatsThread.h"

#include <cassert>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <chrono>

using namespace webclient;

/// Shared stats thread
StatsThread StatsThread::shared;

/**
 * Initializes the stats thread and internal data structures.
 * 
 * The thread is not started yet.
 */
StatsThread::StatsThread()
{
    // set up the quit event
    this->quitEvent = CreateEvent(nullptr, false, false, L"StatsThreadQuit");
    if (this->quitEvent == (HANDLE) ERROR_INVALID_HANDLE) {
        std::string msg = "CreateEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }

    // set up thread
    this->thread = CreateThread(nullptr, kStackSize, StatsThreadEntry, this, CREATE_SUSPENDED, nullptr);
    assert(this->thread);

    SetThreadPriority(this->thread, THREAD_PRIORITY_ABOVE_NORMAL);
}

/**
 * @brief Cleans up all allocated resources and kills the thread, if it's still running.
*/
StatsThread::~StatsThread()
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
    CloseHandle(this->quitEvent);
}

/**
 * @brief Starts the stats thread.
*/
void StatsThread::_startThread()
{
    using namespace std::chrono;

    // record start time of stats request
    this->startTime = steady_clock::now();
    this->bandwidthTime = steady_clock::now();

    // actually resume thread
    ResumeThread(this->thread);
}

/**
 * @brief Indicates to the stats thread it should quit.
*/
void StatsThread::_signalQuit() {
    if (!SetEvent(this->quitEvent)) {
        std::string msg = "SetEvent(): " + GetLastError();
        throw std::runtime_error(msg);
    }
}


/**
 * @brief Trampoline to jump into the class main method
 * @param ctx Context passed to thread creation
*/
DWORD WINAPI webclient::StatsThreadEntry(LPVOID ctx)
{
    static_cast<StatsThread*>(ctx)->threadMain();
    return 0;
}
/**
 * Thread entry point
 */
void StatsThread::threadMain()
{
    using namespace std::chrono;
    // wait on the quit event
    while (WaitForSingleObject(this->quitEvent, 2000) == WAIT_TIMEOUT)
    {
        std::stringstream str;

        // update some statistics
        this->recalculateBandwidth();

        // number of seconds elapsed
        auto end = steady_clock::now();
        duration<double> secs = end - this->startTime;

        // print seconds and number of threads
        str << "[" << std::setw(3) << ((int)secs.count()) << "] ";
        str << std::setw(4) << this->state.numThreads << " ";

        // number of URLs in queue, host unique URLs
        str << "Q " << std::setw(6) << (this->state.queueSize - this->state.queuePulled) << " ";
        str << "E " << std::setw(7) << this->state.queuePulled << " ";
        str << "H " << std::setw(6) << this->state.uniqueHosts << " ";

        // number of DNS lookups and unique IPs
        str << "D " << std::setw(6) << this->state.numDnsLookups << " ";
        str << "I " << std::setw(5) << this->state.uniqueIps << " ";

        // URLs that passed the robots check, successful URLs, number of links
        str << "R " << std::setw(5) << this->state.robotsCheckPassed << " ";
        str << "C " << std::setw(5) << this->state.successPages << " ";
        str << "L " << std::setw(6) << (this->state.numLinks / 1000) << "K ";

        str << std::endl;

        // bandwidth string
        str << "\t*** crawling " << std::setprecision(4) << this->crawlSpeed << " pps @ " 
            << (this->bandwidth / 1024.f / 1024.f) << " Mbps";

        std::cout << str.str() << std::endl << std::flush;
    }

    // print final stats
    this->printFinalStats();

    // clean up
    std::cout << std::endl << std::flush;
}

/**
 * @brief Recalculates the bandwidth, based on the number of bytes inscreted since the last time
 * this function ran.
 * 
 * Should only be called by stats thread.
*/
void StatsThread::recalculateBandwidth()
{
    using namespace std::chrono;

    // capture values
    unsigned long bytes = this->state.bytesRx;
    unsigned long pages = this->state.contentPagesAttempted;
    auto time = steady_clock::now();

    // calculate difference since last time
    unsigned long bytesDiff = bytes - this->bandwidthBytes;
    unsigned long pagesDiff = pages - this->bandwidthPages;
    auto timeDiff = duration_cast<duration<double>>(time - this->bandwidthTime);

    // calculate bandwidth (bits per sec)
    this->bandwidth = ((double)bytesDiff * 8.0) / timeDiff.count();
    this->crawlSpeed = ((double)pagesDiff * 8.0) / timeDiff.count();

    this->bandwidthPages = pages;
    this->bandwidthBytes = bytes;
    this->bandwidthTime = time;
}

/**
 * @brief Prints the final end of run statistics.
*/
void StatsThread::printFinalStats()
{
    using namespace std::chrono;

    std::stringstream str;

    // number of seconds elapsed
    auto end = steady_clock::now();
    auto secs = duration_cast<duration<double>>(end - this->startTime).count();

    // URL count and frequency
    str << "Extracted " << this->state.queueSize << " URLs @ " 
        << (size_t)(((double)this->state.queueSize) / secs) << "/s" << std::endl;
    // DNS resolution frequency
    str << "Looked up " << this->state.numDnsLookups << " DNS names @ "
        << (size_t)(((double)this->state.numDnsLookups) / secs) << "/s" << std::endl;
    // Number of robots files
    str << "Hit " << this->state.robotsAttempted << " robots @ "
        << (size_t)(((double)this->state.robotsAttempted) / secs) << "/s" << std::endl;

    // total pages crawled
    str << "Crawled " << this->state.successPages << " pages @ "
        << (size_t)(((double)this->state.successPages) / secs) << "/s " << "(" 
        << (((double) this->state.bytesRx) / 1024.f / 1024.f) << " MB)" << std::endl;

    // parsed links
    str << "Parsed " << this->state.numLinks << " links @ "
        << (size_t)(((double)this->state.numLinks) / secs) << "/s" << std::endl;

    // HTTP code breakdown
    str << "HTTP codes: ";
    str << "2xx = " << this->state.httpStatusCodes[2] << " ";
    str << "3xx = " << this->state.httpStatusCodes[3] << " ";
    str << "4xx = " << this->state.httpStatusCodes[4] << " ";
    str << "5xx = " << this->state.httpStatusCodes[5] << " ";
    str << "other = " << this->state.httpStatusCodes[0] << " ";

    // print it
    std::cout << str.str() << std::flush;
}