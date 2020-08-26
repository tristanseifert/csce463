// WebClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "pch.h"

#include <iostream>
#include <chrono>

#include "URL.h"
#include "HTTPClient.h"

using namespace webclient;

// what kind of idiot decided this is how you link against libraries
#pragma comment(lib, "ws2_32.lib")

/**
 * Performs general initialization of stuff like WinSock (more like WinSuck lmao)
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
 * Program entry point
 */
int main(int argc, const char** argv)
{
    using namespace std;

    int status = -1;
    
    URL url;
    struct sockaddr_storage addr = {0};

    HTTPClient client;
    HTTPClient::Response res;

    // perform initialization
    CommonInit();

    // parse the URL
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " [url]" << std::endl;
        goto beach;
    }

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
    } catch (std::exception &e) {
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
        client.connect((sockaddr &) addr);
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
                  << " ms with " << res.getPayloadSize() << " bytes" 
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
        const size_t codeLen = res.getPayloadSize();

        auto base = res.getUrl().toString();
        char* baseUrl = const_cast<char *>(base.c_str());
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
    std::cout << std::endl << "----------------------------------------" << std::endl;
    std::cout << res.getResponseHeader() << std::endl;
    status = 0;

    // cleanup
beach:;
    res.release();

    WSACleanup();
    return status;
}