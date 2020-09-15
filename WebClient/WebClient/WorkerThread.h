#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <string>

namespace webclient {
DWORD WINAPI WorkerThreadEntry(LPVOID);

class URL;

/**
 * @brief Pulls URLs from the work queue and crawls them in a background thread, until the queue
 * is drained.
*/
class WorkerThread {
    friend DWORD WINAPI WorkerThreadEntry(LPVOID);

public:
    WorkerThread();
    ~WorkerThread();

    /// Begins execution of the thread.
    void start()
    {
        this->_startThread();
    }

    /// Waits for the thread to finish execution.
    void wait()
    {
        this->_waitForQuit();
    }

private:
    void _startThread();
    void _signalQuit();
    void _waitForQuit();

    void threadMain();
    void processUrl(const std::string&);
    bool checkHostUniqueness(const URL&);
    bool checkAddressUniqueness(URL&, struct sockaddr_storage *);

private:
    /// size of the worker thread stack, in bytes.
    constexpr static const size_t kStackSize = (1024 * 128);

private:
    // handle to the stats thread
    HANDLE thread = INVALID_HANDLE_VALUE;
    // signalled when quit is desired
    HANDLE quitEvent = INVALID_HANDLE_VALUE;

};
}


#endif