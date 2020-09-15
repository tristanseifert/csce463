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
        str << "\t*** crawling " << std::setprecision(1) << this->crawlSpeed << " pps @ " 
            << (this->bandwidth / 1024.f / 1024.f) << " Mbps";

        std::cout << str.str() << std::endl << std::flush;
    }

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
    unsigned long pages = this->state.numRequests;
    auto time = steady_clock::now();

    // calculate difference since last time
    unsigned long bytesDiff = bytes - this->bandwidthBytes;
    unsigned long pagesDiff = pages - this->bandwidthPages;
    auto timeDiff = time - this->bandwidthTime;

    // calculate bandwidth (bits per sec)
    this->bandwidth = ((double)bytesDiff * 8.0) / timeDiff.count();
    this->crawlSpeed = ((double)pagesDiff * 8.0) / timeDiff.count();

    this->bandwidthPages = pages;
    this->bandwidthBytes = bytes;
    this->bandwidthTime = time;
}