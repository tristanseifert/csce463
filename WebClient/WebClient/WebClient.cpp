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

#include "URL.h"
#include "HTTPClient.h"

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
        client.connect((sockaddr&)addr);
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

static int ValidateUrl(URL& url, std::unordered_set<std::string>& hosts,
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
 * @brief Evaluates a single URL.
*/
static int DoMultipleUrlsStep(URL& url, std::unordered_set<std::string>& hosts, 
    std::unordered_set<std::string>& ips)
{
    using namespace std;

    int status;
    struct sockaddr_storage addr = { 0 };

    // validate the URL (and bail if not valid)
    status = ValidateUrl(url, hosts, ips, &addr);
    if (status)
        return status;

    // request robots file

    // successfully dealt with this URL
    return status;
}

/**
 * @brief Performs crawling over all URLs in the given file.
 * 
 * The entire list of files is loaded into memory, then deduplicated by hostname.
*/
static int DoMultipleUrls(int argc, const char** argv)
{
    using namespace std;

    int status = -1;

    URL url;
    std::vector<URL> urls;
    std::unordered_set<string> hosts;
    std::unordered_set<string> ips;

    // validate args
    if (atoi(argv[1]) != 1) {
        std::cerr << "unsupported number of threads: " << argv[1] << std::endl;
        return -1;
    }

    // open URL file for reading
    ifstream file(argv[2]);
    if (!file.is_open()) {
        return -1;
    }

    filesystem::path p { argv[2] };
    cout << "Opened " << p.filename() << " with size " << filesystem::file_size(p) << endl;

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
        } catch(std::exception &e) {
            cerr << "\t  Parsing URL... failed: " << e.what() << endl;    
            continue;
        }

        // fetch this URL
        status = DoMultipleUrlsStep(url, hosts, ips);

        // prepare for next one
        cout << endl;
    }

    // cleanup
beach:;
    return status;
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