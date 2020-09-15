/*
 * CSCE 463-500 Homework 1, part 1
 * 
 * Main function of the homework assignment and init functions for WinSock.
 * 
 * @author Tristan Seifert
 */
#include "pch.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <memory>

#include "URL.h"
#include "HTTPClient.h"
#include "StatsThread.h"
#include "WorkQueue.h"
#include "WorkerThread.h"

using namespace webclient;

#pragma comment(lib, "ws2_32.lib")

/**
 * Performs general initialization of stuff like WinSock
 */
static void CommonInit()
{
    int err;
    WSAData wsDetails;

    // winsock
    err = WSAStartup(MAKEWORD(2, 2), &wsDetails);

    if (err != 0) {
        std::cerr << "WSAStartup() failed: " << err << std::endl;
        exit(-1);
    }
}

/**
 * @brief Downloads a single URL. This is basically implementing the behavior of part 1.
*/
static int DoSingleUrl(int argc, const char** argv)
{
    using namespace std;

    int status = -1;

    URL url;
    struct sockaddr_storage addr = { 0 };

    HTTPClient client;
    HTTPClient::Response res;

    try {
        std::cout << "URL: " << argv[1] << std::endl;

        // parse URL and validate scheme
        url = URL(argv[1]);
        if (url.getScheme() != "http") {
            throw std::runtime_error("Invalid scheme");
        }

        std::cout << "\t  Parsing URL... host " << url.getHostname() << ", port "
                  << url.getPort() << ", request " << url.getPath()
                  << std::endl;
    } catch (std::exception& e) {
        std::cerr << "\t  Parsing URL... failed: " << e.what() << std::endl;
        goto beach;
    }

    // attempt to resolve the hostname
    try {
        auto start = std::chrono::steady_clock::now();

        std::cout << "\t  Performing DNS lookup..." << std::flush;
        url.resolve(&addr);

        // calculate total time taken and print result
        auto end = chrono::steady_clock::now();
        auto diff = end - start;
        auto ms = chrono::duration<double, milli>(diff).count();

        char buffer[INET6_ADDRSTRLEN] = { 0 };
        PCSTR addrStr = nullptr;

        if (addr.ss_family == AF_INET) {
            auto addrV4 = reinterpret_cast<sockaddr_in*>(&addr);
            addrStr = inet_ntop(addrV4->sin_family, &addrV4->sin_addr, buffer,
                INET6_ADDRSTRLEN);
        }

        std::cout << " done in " << ms << " ms, found " << addrStr << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        goto beach;
    }

    // connect and retrieve the document
    try {
        // connect to the server
        std::cout << "\t* Connecting to server... " << std::flush;
        auto start = chrono::steady_clock::now();
        client.connect((sockaddr *) &addr);
        auto end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms" << std::endl;

        // fetch the page body
        std::cout << "\t  Loading... " << std::flush;
        start = chrono::steady_clock::now();
        res = client.fetch(url);
        end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms with " << res.getTotalReceived() << " bytes"
                  << std::endl;

        // verify header code
        std::cout << "\t  Verifying header... status code " << res.getStatus() << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        goto beach;
    }

    // parse page contents if 2xx code
    if (res.getStatus() >= 200 && res.getStatus() <= 299) {
        std::cout << "\t+ Parsing page... " << std::flush;
        auto start = chrono::steady_clock::now();

        // parse the page conents
        HTMLParserBase parser;

        char* code = reinterpret_cast<char*>(res.getPayload());
        const size_t codeLen = res.getPayloadSize() + 1;

        auto base = res.getUrl().toString();
        char* baseUrl = const_cast<char*>(base.c_str());
        size_t baseUrlLen = base.size();

        int nLinks;
        char* linkBuffer = parser.Parse(code, codeLen, baseUrl, baseUrlLen, &nLinks);

        // print statistics
        auto end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms with " << nLinks << " links" << std::endl;
    }

    // print the headers
    std::cout << std::endl
              << "----------------------------------------" << std::endl;
    std::cout << res.getResponseHeader() << std::endl;
    status = 0;

    // cleanup
beach:;
    res.release();
    return status;
}

/**
 * @brief Determines whether the given URL is unique on host and IP.
*/
static int ValidateUrlUnique(URL& url, std::unordered_set<std::string>& hosts,
    std::unordered_set<std::string>& ips, struct sockaddr_storage *addr)
{
    using namespace std;

    std::string addrString;

    // check host uniqueness
    cout << "\t  Checking host uniqueness...";
    if (hosts.find(url.getHostname()) != hosts.end()) {
        cout << " failed" << endl;
        return -1;
    } else {
        hosts.insert(url.getHostname());
        cout << " passed" << endl;
    }

    // perform DNS resolution
    try {
        auto start = std::chrono::steady_clock::now();

        std::cout << "\t  Performing DNS lookup..." << std::flush;
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

        std::cout << " done in " << ms << " ms, found " << addrString << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        return -1;
    }

    // then, ensure the IP is unique
    cout << "\t  Checking IP uniqueness...";
    if (ips.find(addrString) != ips.end()) {
        cout << " failed" << endl;
        return -1;
    } else {
        ips.insert(addrString);
        cout << " passed" << endl;
    }

    // IP is valid
    return 0;
}

/**
 * @brief Checks whether the given URL allows crawling, by checking for the
 * existence of a robots file.
 * 
 * @return 0 if no error and there is no robots file; negative for internal
 * errors, positive number to indicate that there is a robots file.
*/
static int CheckUrlRobots(const URL& inUrl, const struct sockaddr_storage* addr)
{
    using namespace std;

    HTTPClient client;
    HTTPClient::Response res;

    // create the robots URL
    URL url = inUrl;
    url.setPath("/robots.txt");

    // connect and attempt to retrievethe document
    try {
        // connect to the server
        std::cout << "\t  Connecting to server (for robots)... " << std::flush;
        auto start = chrono::steady_clock::now();
        client.connect((sockaddr*) addr);
        auto end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms" << std::endl;

        // fetch the page body
        std::cout << "\t  Loading... " << std::flush;
        start = chrono::steady_clock::now();
        res = client.fetch(url, HTTPClient::HEAD, (1024 * 16));
        end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms with " << res.getTotalReceived() << " bytes"
                  << std::endl;

        // verify header code
        std::cout << "\t  Verifying header... status code " << res.getStatus() << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        return -1;
    }

    // success for 4xx codes
    return (res.getStatus() >= 400 && res.getStatus() <= 499) ? 0 : 1;
}

/**
 * @brief Fetches a single URL and prints statistics about it.
 * 
 * @return Number of links on the page, or a negative error code
*/
static int FetchUrl(const URL& inUrl, const struct sockaddr_storage* addr)
{
    using namespace std;

    HTTPClient client;
    HTTPClient::Response res;

    // connect and retrieve the document
    try {
        // connect to the server
        std::cout << "\t* Connecting to server... " << std::flush;
        auto start = chrono::steady_clock::now();
        client.connect((sockaddr*) addr);
        auto end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms" << std::endl;

        // fetch the page body
        std::cout << "\t  Loading... " << std::flush;
        start = chrono::steady_clock::now();
        res = client.fetch(inUrl, HTTPClient::GET, (1024 * 1024 * 2));
        end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms with " << res.getTotalReceived() << " bytes"
                  << std::endl;

        // verify header code
        std::cout << "\t  Verifying header... status code " << res.getStatus() << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        return -1;
    }

    // parse page contents if 2xx code
    if (res.getStatus() >= 200 && res.getStatus() <= 299) {
        std::cout << "\t+ Parsing page... " << std::flush;
        auto start = chrono::steady_clock::now();

        // parse the page conents
        HTMLParserBase parser;

        char* code = reinterpret_cast<char*>(res.getPayload());
        const size_t codeLen = res.getPayloadSize() + 1;

        auto base = res.getUrl().toString();
        char* baseUrl = const_cast<char*>(base.c_str());
        size_t baseUrlLen = base.size();

        int nLinks;
        char* linkBuffer = parser.Parse(code, codeLen, baseUrl, baseUrlLen, &nLinks);

        // print statistics
        auto end = chrono::steady_clock::now();
        std::cout << " done in "
                  << chrono::duration<double, milli>(end - start).count()
                  << " ms with " << nLinks << " links" << std::endl;
        return nLinks;
    }

    // if we get here, the download succeded but we didn't parse it
    return 0;

}

/**
 * @brief Evaluates a single URL.
*/
static int DoMultipleUrlsStep(URL& url, std::unordered_set<std::string>& hosts, 
    std::unordered_set<std::string>& ips)
{
    using namespace std;

    int status;
    struct sockaddr_storage addr = { 0 };

    // ensure the URL is unique (and bail if not)
    status = ValidateUrlUnique(url, hosts, ips, &addr);
    if (status)
        return status;

    // request robots file
    status = CheckUrlRobots(url, &addr);
    if (status > 0) {
        return 0;
    } else if(status < 0) {
        return status;
    }

    // fetch the url
    status = FetchUrl(url, &addr);
    if (status < 0) {
        return status;
    }

    // successfully dealt with this URL
    return 0;
}

/**
 * @brief Fetches all URLs in the given file using single threaded IO.
 * @param file File containing a list of URLs
*/
static int DoMultipleUrlsSingleThreaded(std::ifstream &file)
{
    using namespace std;

    int status;
    URL url;
    std::unordered_set<string> hosts;
    std::unordered_set<string> ips;

    // read each line
    string line;
    while (getline(file, line)) {
        if (line.empty())
            continue;

        // parse the line as an url
        cout << "URL: " << line << endl;

        try {
            url = URL(line);
            if (url.getScheme() != "http") {
                throw runtime_error("Invalid scheme");
            }

            cout << "\t  Parsing URL... host " << url.getHostname() << ", port "
                 << url.getPort() << ", request " << url.getPath() << endl;
        } catch (std::exception& e) {
            cerr << "\t  Parsing URL... failed: " << e.what() << endl;
            continue;
        }

        // fetch this URL
        status = DoMultipleUrlsStep(url, hosts, ips);

        // prepare for next one
        cout << endl;
    }

    // success!
    return 0;
}

/**
 * @brief Performs crawling over all URLs in the given file.
 * 
 * The entire list of files is loaded into memory, then deduplicated by hostname.
*/
static int DoMultipleUrls(int argc, const char** argv)
{
    int status = -1;
    int numThreads = atoi(argv[1]);

    URL url;
    std::vector<URL> urls;
    std::vector<std::shared_ptr<WorkerThread>> workers;

    // validate args
    if (numThreads < 1) {
        std::cerr << "unsupported number of threads: " << argv[1] << std::endl;
        return -1;
    }

    // open URL file for reading
    std::ifstream file(argv[2]);
    if (!file.is_open()) {
        std::cerr << "Failed to open '" << argv[2] << "'" << std::endl;
        return -1;
    }

    std::filesystem::path p { argv[2] };
    std::cout << "Opened " << p.filename() << " with size " << std::filesystem::file_size(p) 
              << std::endl;

    // handle single threaded mode
    if (numThreads == 1) {
        return DoMultipleUrlsSingleThreaded(file);
    }
    // multithreaded mode
    else {
        size_t numUrls = 0;

        // read all URLs into a buffer
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            WorkQueue::shared.pushUrlStringUnsafe(line);
            numUrls++;
        }
        file.close();

        std::cout << "Read " << numUrls << " URLs from file" << std::endl;

        // create worker threads
        for (size_t i = 0; i < numThreads; i++) {
            workers.push_back(std::make_shared<WorkerThread>());
        }

        // start stats thread and then the worker threads
        StatsThread::begin();

        for (auto worker : workers) {
            worker->start();
        }

        // wait for worker threads to quit and quit stats thread
        for (auto worker : workers) {
            worker->wait();
        }
    
        workers.clear();

        Sleep(2000);
        StatsThread::quit();
    }

    // success!
    return 0;
}

/**
 * Program entry point
 */
int main(int argc, const char** argv)
{
    int status = -1;

    // perform initialization
    CommonInit();

    // invoke the correct method
    if (argc == 2) {
        status = DoSingleUrl(argc, argv);
    } else if (argc == 3) {
        status = DoMultipleUrls(argc, argv);
    } else {
        std::cerr << "usage: " << argv[0] << " [url]" << std::endl;
        std::cerr << "       " << argv[0] << " [threads] [url list file path]" << std::endl;
        goto beach;
    }

    // cleanup
beach:;
    WSACleanup();
    return status;
}