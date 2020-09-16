#include "pch.h"
#include "WorkerThread.h"
#include "WorkQueue.h"
#include "StatsThread.h"
#include "URL.h"
#include "HTTPClient.h"
#include "ParserPool.h"
#include "HTMLParserBase.h"

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
    int links;

    // parse the URL and validate scheme
    url = URL(urlStr);
    if (url.getScheme() != "http") {
        throw std::runtime_error("Invalid scheme");
    }
    // check that the host is unique
    if (!this->checkHostUniqueness(url)) {
//        std::cerr << "Failed host uniqueness test: " << url.toString() << std::endl;
        return;
    }
    // resolve the host, and validate the IP is unique
    if (!this->checkAddressUniqueness(url, &addr)) {
//        std::cerr << "Failed IP uniqueness test: " << url.toString() << std::endl;
        return;
    }

    // fetch the robots file and ensure we can crawl it
    if (!this->checkRobots(url, &addr)) {
//        std::cerr << "Failed robots test: " << url.toString() << std::endl;
        return;
    }
    StatsThread::shared.state.robotsCheckPassed++;

    // go ahead and fetch the body. the number of links found is returned
    links = this->fetchPage(url, &addr);
    StatsThread::shared.state.numLinks += links;
}

/**
 * @brief Checks whether the given website allows crawling (by checking for robots.txt missing)
*/
bool WorkerThread::checkRobots(const URL& inUrl, const struct sockaddr_storage* addr)
{
    using namespace std;

    HTTPClient client;
    HTTPClient::Response res;

    // create the robots URL
    URL url = inUrl;
    url.setPath("/robots.txt");

    // connect to the server and make HEAD request
    client.connect((sockaddr*)addr);

    StatsThread::shared.state.robotsAttempted++;
    res = client.fetch(url, HTTPClient::HEAD, (1024 * 16));

    // success for 4xx codes
    bool success = (res.getStatus() >= 400 && res.getStatus() <= 499) ? 0 : 1;
    res.release();

    return success;
}


/**
 * @brief Fetches the page content, and parses it to figure out how many links are in the page.
 * 
 * @return Number of links on page
*/
size_t WorkerThread::fetchPage(const URL& inUrl, const struct sockaddr_storage* addr)
{
    using namespace std;

    size_t numLinks = 0;

    HTTPClient client;
    HTTPClient::Response res;
    char* readPtr;

    std::vector<std::string> links;

    // connect to the server and make GET request
    client.connect((sockaddr*)addr);

    StatsThread::shared.state.contentPagesAttempted++;
    res = client.fetch(inUrl, HTTPClient::GET, (1024 * 1024 * 2));

    // success! parse the HTML for links
    if (res.getStatus() >= 200 && res.getStatus() <= 299) {
        StatsThread::shared.state.successPages++;

        ParserPool::shared.getParser([this, &res, &numLinks, &links](auto parser) {
            char* readPtr;

            // get the HTML payload
            char* code = reinterpret_cast<char*>(res.getPayload());
            const size_t codeLen = res.getPayloadSize() + 1;

            // get base url as string
            auto base = res.getUrl().toString();
            char* baseUrl = const_cast<char*>(base.c_str());
            size_t baseUrlLen = base.size();

            // parse number of links
            int nLinks;
            char* linkBuffer = parser->Parse(code, codeLen, baseUrl, baseUrlLen, &nLinks);

            // create vector of strings
            if (!WorkQueue::shared.captureLinks || nLinks < 0)
                goto skipWriteLinks;

            readPtr = linkBuffer;
            for (size_t i = 0; i < nLinks; i++) {
                // extract the link
                size_t len = strlen(readPtr);
                auto str = std::string(readPtr, len);

                links.push_back(str);

                // goto next one (length + zero byte)
                readPtr += len + 1;
            }

skipWriteLinks:;
            // done!
            numLinks = nLinks;
        });
    }

    // write the links
    WorkQueue::shared.lockedLinksAccess([this, &links, &inUrl](auto allLinks) {
        allLinks->reserve(allLinks->size() + links.size());

        std::string header = "\n*** " + inUrl.toString();
        allLinks->push_back(header);

        for (auto link : links) {
            allLinks->push_back(link);
        }
    });

    // clean up
cleanup:;
    res.release();

    return numLinks;
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
        if (hosts->find(url.getHostname()) != hosts->end()) {
            unique = false;
        } else {
            hosts->insert(url.getHostname());

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
        if (ips->find(addrString) != ips->end()) {
            unique = false;
        } else {
            ips->insert(addrString);

            StatsThread::shared.state.uniqueIps++;
            unique = true;
        }
    });

    return unique;
}