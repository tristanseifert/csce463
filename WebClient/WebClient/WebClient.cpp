// WebClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "pch.h"

#include <iostream>
#include <chrono>

#include "URL.h"

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
    URL url;
    struct sockaddr_in addr;

    // perform initialization
    CommonInit();

    // parse the URL
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " [url]" << std::endl;
        return -1;
    }

    try {
        std::cout << "URL: " << argv[1] << std::endl;
        
        // parse URL and validate scheme
        url = URL(argv[1]);
        if (url.getScheme() != "http") {
            throw std::runtime_error("Invalid scheme");
        }

        std::cout << "\tParsing URL... host " << url.getHostname() << ", port " 
                  << url.getPort() << ", request " << url.getPath() 
                  << std::endl;
    } catch (std::exception &e) {
        std::cerr << "\tParsing URL... failed: " << e.what() << std::endl;
        return -1;
    }

    // attempt to resolve the hostname
    try {
        auto start = std::chrono::steady_clock::now();

        std::cout << "\tPerforming DNS lookup...";
        url.resolve(&addr);

        // calculate total time taken and print result
        auto end = std::chrono::steady_clock::now();
        auto diff = end - start;
        auto ms = std::chrono::duration<double, std::milli>(diff).count();

        char buffer[INET6_ADDRSTRLEN] = { 0 };
        const char* addrStr = inet_ntop(addr.sin_family, &addr.sin_addr, buffer, 
            INET6_ADDRSTRLEN);

        std::cout << " done in " << ms << " ms, found " << addrStr << std::endl;
    } catch (std::exception& e) {
        std::cerr << " failed: " << e.what() << std::endl;
        return -1;
    }

    // connect and retrieve the document

    // cleanup
    WSACleanup();
}