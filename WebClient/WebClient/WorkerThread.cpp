#include "WorkerThread.h"
#include "WorkQueue.h"
#include "StatsThread.h"

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
        std::cout << "URL string: " << urlStr << std::endl;
    }

    // exiting
    StatsThread::shared.state.numThreads--;
}